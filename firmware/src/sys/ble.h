#pragma once
// DAXEN VANE sistem katmanı · Bluetooth: telefonla bağlantı.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// Bluetooth (telefonla internetsiz bağlantı, bildirim ve müzik) — docs/bluetooth.md
// ============================================================================
// Kart çevre birimidir; telefon bağlanır ve "H" mesajıyla eşleşme anahtarını yollar. Kart ilk
// eşleşen telefonun anahtarını saklar; başka telefon yalnızca Ayarlar → Bluetooth eşleşme
// kipindeyken eşleşebilir. Mesajlar 0xA5, tür, uzunluk (2 bayt) ve veriden oluşan çerçevelerdir.
static NimBLEServer* bleServer = nullptr;
static NimBLECharacteristic* bleTxChar = nullptr;
static volatile uint16_t bleMtu = 23;
static volatile uint16_t bleConn = 0;
static std::vector<uint8_t> bleRx;
static uint32_t bleDropAt = 0, bleInflightAt = 0, bleStatusAt = 0;

class BleServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    bleConn = d->conn_handle;
    bleMtu = 23;
    {
      Lock l;
      bleRx.clear();
    }
    bleConnected = true;
  }
  void onDisconnect(NimBLEServer* s) override {
    bleConnected = false;
    bleLinked = false;
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc* d) override { bleMtu = mtu; }
};

// Gelen veri tamponu açılışta bir kez ayrılır ve hiç büyümez: sonradan büyük bellek istemek,
// bellek parçalandığında kartı çökertiyordu.
static const size_t BLE_RX_CAP = 16400;
class BleRxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    NimBLEAttValue v = c->getValue();
    Lock l;
    if (bleRx.size() + v.length() <= BLE_RX_CAP) bleRx.insert(bleRx.end(), v.data(), v.data() + v.length());
    else bleRx.clear();  // taşma: akış bozuldu, baştan
  }
};

static String bleTokenLoad() {
  Lock l;
  prefs.begin("cs", true);
  String t = prefs.getString("btok", "");
  prefs.end();
  return t;
}

static void bleTokenSave(const String& t) {
  Lock l;
  prefs.begin("cs", false);
  if (t.length()) prefs.putString("btok", t);
  else prefs.remove("btok");
  prefs.end();
  bleHasToken = t.length() > 0;
}

static void bleBegin() {
  bleRx.reserve(BLE_RX_CAP);
  if (!artPx) artPx = (uint16_t*)malloc(ART_PX * ART_PX * 2);
  bleHasToken = bleTokenLoad().length() > 0;
  NimBLEDevice::init(BLE_NAME);
  NimBLEDevice::setMTU(247);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new BleServerCb());
  NimBLEService* svc = bleServer->createService(BLE_SVC);
  NimBLECharacteristic* rx = svc->createCharacteristic(BLE_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new BleRxCb());
  bleTxChar = svc->createCharacteristic(BLE_TX, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SVC);
  adv->setScanResponse(true);
  adv->start();
}

// Çerçeve yığında (stack) parça parça kurulur; gönderirken bellek ayrılmaz.
static void bleSend(char type, const uint8_t* data, size_t n) {
  if (!bleConnected || !bleTxChar || n > 16000) return;
  uint8_t head[4] = {0xA5, (uint8_t)type, (uint8_t)(n & 0xFF), (uint8_t)((n >> 8) & 0xFF)};
  uint8_t buf[244];
  size_t chunk = std::min<size_t>(sizeof buf, std::max<int>(20, (int)bleMtu - 3));
  size_t total = n + 4, sent = 0;
  while (sent < total) {
    size_t m = std::min(chunk, total - sent);
    for (size_t i = 0; i < m; i++) {
      size_t k = sent + i;
      buf[i] = k < 4 ? head[k] : data[k - 4];
    }
    bleTxChar->setValue(buf, m);
    bleTxChar->notify();
    sent += m;
    if (total > chunk) delay(4);
  }
}

static void bleSendJson(char type, JsonDocument& d) {
  String s;
  serializeJson(d, s);
  bleSend(type, (const uint8_t*)s.c_str(), s.length());
}

static void bleSendStatus() {
  JsonDocument d;
  d["fw"] = "fw-" + String(FW_BUILD);
  d["variant"] = FW_VARIANT;
  d["heap"] = ESP.getFreeHeap();
  d["minHeap"] = ESP.getMinFreeHeap();
  d["reset"] = bootReason;
  d["sd"] = sdOk;
  d["queue"] = queueCount();
  d["wifi"] = WiFi.status() == WL_CONNECTED;
  bleSendJson('S', d);
  bleStatusAt = millis();
}

static void bleSendTimer() {
  SharedTimer t;
  {
    Lock l;
    t = outTimer;
    bleTimerPending = false;
  }
  if (!t.rev) return;
  JsonDocument d;
  d["on"] = t.on;
  if (t.on) {
    d["itemId"] = t.itemId;
    d["date"] = t.date;
    d["ders"] = t.ders;
    d["konu"] = t.konu;
    d["dk"] = t.dk;
    d["startedAt"] = t.startedAt;
    d["acc"] = t.acc;
  }
  d["by"] = "device";
  d["rev"] = t.rev;
  bleSendJson('T', d);
}

// Kuyruktaki olaylar 10'arlı paketlerle telefona gider; telefon "a" ile onaylayınca silinir.
static void bleFlushEvents() {
  static uint32_t lastTry = 0;
  if (bleInflight) {
    if (millis() - bleInflightAt < 8000) return;
    bleInflight = 0;  // onay gelmedi; yeniden gönderilir (anahtarlar aynı, çift kayıt olmaz)
  }
  if (millis() - lastTry < 500) return;
  lastTry = millis();
  {
    Lock l;
    if (fbFlushing) return;
    bleInflight = -1;  // bulutun aynı anda göndermesini engelle
  }
  std::vector<String> batch = queuePeek(10);
  if (batch.empty()) {
    bleInflight = 0;
    return;
  }
  String body = "{";
  int valid = 0;
  for (auto& line : batch) {
    JsonDocument t;
    if (deserializeJson(t, line) || !t.is<JsonObject>()) continue;
    if (valid++) body += ',';
    body += '"';
    body += eventKey(line);
    body += "\":";
    body += line;
  }
  body += '}';
  if (!valid) {
    queueDrop(batch.size());
    bleInflight = 0;
    return;
  }
  bleInflight = batch.size();
  bleInflightAt = millis();
  bleSend('E', (const uint8_t*)body.c_str(), body.length());
}

static void bleOnLinked() {
  bleLinked = true;
  bleInflight = 0;
  bleSendStatus();
  dataDirty = true;
}

static void bleOnUnlinked() {
  bleLinked = false;
  bleInflight = 0;
  media.on = false;
  if (accentOn) {
    accentOn = false;
    uiPaletteChanged();
  }
  dataDirty = true;
}

static void blePairMode() { blePairUntil = millis() + BLE_PAIR_MS; }

static void bleForget() {
  bleTokenSave("");
  if (bleConnected) bleDropAt = millis() + 200;
}

static String bleStatusText() {
  if (bleLinked) return "Bağlı";
  if (blePairUntil && (int32_t)(blePairUntil - millis()) > 0) return "Eşleşme kipi";
  if (!bleHasToken) return "Eşleşmedi";
  return "Bekleniyor";
}

static bool artPending = false;
static void bleImage(char type, const uint8_t* p, size_t len) {
  if (len < 8) return;
  uint32_t id = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
  int w = p[4] | (p[5] << 8), h = p[6] | (p[7] << 8);
  if (len != 8 + (size_t)w * h * 2) return;
  if (type == 'I' && w == ICON_PX && h == ICON_PX) {
    IconImg* ic = findIcon(id);
    if (!ic) {
      if (icons.size() >= 12) {  // en uzun süredir kullanılmayan çıkar
        auto old = std::min_element(icons.begin(), icons.end(), [](IconImg* a, IconImg* b) { return a->used < b->used; });
        delete *old;
        icons.erase(old);
      }
      ic = new (std::nothrow) IconImg();
      if (!ic) return;
      icons.push_back(ic);
    }
    ic->id = id;
    ic->used = millis();
    memcpy(ic->px, p + 8, ICON_PX * ICON_PX * 2);
  } else if (type == 'A' && w == ART_PX && h == ART_PX && artPx) {
    memcpy(artPx, p + 8, ART_PX * ART_PX * 2);
    artId = id;
    artPending = true;  // çizim kilit dışında yapılır
  }
}

static void bleHandle(char type, const std::vector<uint8_t>& p) {
  if (!bleLinked && type != 'H') return;
  JsonDocument d;
  if (deserializeJson(d, (const char*)p.data(), p.size())) return;
  switch (type) {
    case 'H': {  // merhaba + eşleşme anahtarı
      String tok = String((const char*)(d["token"] | ""));
      String saved = bleTokenLoad();
      bool pairing = blePairUntil && (int32_t)(blePairUntil - millis()) > 0;
      bool ok = false, paired = false;
      if (tok.length() >= 16) {
        if (saved.length() && tok == saved) {
          ok = true;
        } else if (!saved.length() || pairing) {
          bleTokenSave(tok);
          ok = paired = true;
          blePairUntil = 0;
        }
      }
      JsonDocument r;
      r["ok"] = ok;
      r["paired"] = paired;
      r["fw"] = "fw-" + String(FW_BUILD);
      r["variant"] = FW_VARIANT;
      bleSendJson('h', r);
      if (!ok) {
        bleDropAt = millis() + 600;
        break;
      }
      long long t = d["t"].as<long long>();
      if (t > 1700000000000LL && (!timeOk() || llabs(nowMs() - t) > 2000)) {
        struct timeval tv = {(time_t)(t / 1000), (suseconds_t)((t % 1000) * 1000)};
        settimeofday(&tv, nullptr);
      }
      bleOnLinked();
      uiToast(paired ? "Telefonla eşleşti" : "Telefon bağlandı");
      break;
    }
    case 'C': {  // saat
      long long t = d["t"].as<long long>();
      if (t > 1700000000000LL && (!timeOk() || llabs(nowMs() - t) > 2000)) {
        struct timeval tv = {(time_t)(t / 1000), (suseconds_t)((t % 1000) * 1000)};
        settimeofday(&tv, nullptr);
        dataDirty = true;
      }
      break;
    }
    case 'V':
      applyView(d.as<JsonVariantConst>());
      break;
    case 'T':
      takeRemoteTimer(d.as<JsonVariantConst>());
      break;
    case 'a':  // olaylar telefona ulaştı
      if (bleInflight > 0) queueDropSent(bleInflight);
      bleInflight = 0;
      break;
    case 'N': {
      Notif n;
      n.id = String((const char*)(d["id"] | ""));
      n.app = String((const char*)(d["app"] | ""));
      n.title = String((const char*)(d["title"] | ""));
      n.text = String((const char*)(d["text"] | ""));
      n.icon = d["ic"].as<uint32_t>();
      n.at = millis();
      notifs.erase(std::remove_if(notifs.begin(), notifs.end(), [&](const Notif& x) { return x.id == n.id; }), notifs.end());
      notifs.insert(notifs.begin(), n);
      if (notifs.size() > 8) notifs.pop_back();
      uiNotif(n);
      uiNotifsChanged();
      break;
    }
    case 'X': {
      String id = String((const char*)(d["id"] | ""));
      size_t before = notifs.size();
      notifs.erase(std::remove_if(notifs.begin(), notifs.end(), [&](const Notif& x) { return x.id == id; }), notifs.end());
      if (notifs.size() != before) uiNotifsChanged();
      break;
    }
    case 'M': {
      media.on = d["on"] | false;
      media.enabled = d["enabled"] | true;
      media.app = String((const char*)(d["app"] | ""));
      media.title = String((const char*)(d["title"] | ""));
      media.artist = String((const char*)(d["artist"] | ""));
      media.playing = d["playing"] | false;
      media.pos = d["pos"].as<uint32_t>();
      media.dur = d["dur"].as<uint32_t>();
      media.at = millis();
      media.vol = d["vol"] | 0;
      media.volMax = d["volMax"] | 15;
      media.art = d["art"].as<uint32_t>();
      bool hasAcc = media.on && d["accent"].is<JsonArrayConst>() && d["dark"].is<JsonArrayConst>();
      bool accChanged = hasAcc != accentOn;
      if (hasAcc) {
        uint8_t a[3], k[3];
        for (int i = 0; i < 3; i++) {
          a[i] = d["accent"][i] | 0;
          k[i] = d["dark"][i] | 0;
        }
        if (memcmp(a, accRgb, 3) || memcmp(k, darkRgb, 3)) accChanged = true;
        memcpy(accRgb, a, 3);
        memcpy(darkRgb, k, 3);
      }
      accentOn = hasAcc;
      if (accChanged && cfg.coverTheme) uiPaletteChanged();
      else uiMediaChanged(false);
      break;
    }
    case 'U':  // telefon eşleşmeyi kaldırdı
      bleTokenSave("");
      bleOnUnlinked();
      bleDropAt = millis() + 300;
      uiToast("Telefonla eşleşme kaldırıldı");
      break;
    default:
      break;
  }
}

// Ana döngüden çağrılır: gelen çerçeveleri işler, bekleyenleri gönderir.
static void bleProcess() {
  static bool wasLinked = false;
  if (wasLinked && !bleLinked) bleOnUnlinked();
  wasLinked = bleLinked;
  if (bleDropAt && (int32_t)(millis() - bleDropAt) >= 0) {
    bleDropAt = 0;
    if (bleServer && bleConnected) bleServer->disconnect(bleConn);
  }
  for (int k = 0; k < 6; k++) {
    char type = 0;
    std::vector<uint8_t> payload;
    {
      Lock l;
      size_t i = 0;
      while (i < bleRx.size() && bleRx[i] != 0xA5) i++;
      if (i) bleRx.erase(bleRx.begin(), bleRx.begin() + i);
      if (bleRx.size() < 4) break;
      size_t len = bleRx[2] | (bleRx[3] << 8);
      if (len > 16000) {
        bleRx.erase(bleRx.begin());
        continue;
      }
      if (bleRx.size() < 4 + len) break;
      type = (char)bleRx[1];
      if (type == 'I' || type == 'A') {
        if (bleLinked) bleImage(type, bleRx.data() + 4, len);  // görüntü kopyalanmadan işlenir
        type = 0;
      } else {
        payload.assign(bleRx.begin() + 4, bleRx.begin() + 4 + len);
      }
      bleRx.erase(bleRx.begin(), bleRx.begin() + 4 + len);
    }
    if (artPending) {
      artPending = false;
      if (media.art == artId) uiArtChanged();
    }
    if (!type) continue;
    bleHandle(type, payload);
  }
  if (bleLinked) {
    bleFlushEvents();
    if (bleTimerPending) bleSendTimer();
    if (millis() - bleStatusAt > 60000) bleSendStatus();
  }
  if (blePairUntil && (int32_t)(millis() - blePairUntil) > 0) {
    blePairUntil = 0;
    uiDataChanged();
  }
}

static void bleMediaKey(const char* cmd) {
  JsonDocument d;
  d["cmd"] = cmd;
  bleSendJson('K', d);
}


