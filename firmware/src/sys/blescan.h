#pragma once
// DAXEN VANE sistem katmanı · Bluetooth keşfi (gözlemci rolü). Çevredeki cihazların herkese açık
// reklam paketleri okunur; hiçbir cihaza bağlanılmaz, hiçbir şey yazılmaz. Tarama etkindir: çoğu
// cihaz adını tarama yanıtında verdiği için kart, her BLE taramasında olduğu gibi tarama isteği
// gönderir. Telefon bağlıyken tarama penceresi daraltılır ki bağlantı olaylarına yer kalsın.
// Liste en fazla 30 cihaz (yaklaşık 1,7 KB) ve yalnız tarama ile ekran süresince bellekte.
// Geri çağrı NimBLE görevinde çalışır: ortak kilit (mtx) yerine kısa bir spinlock kullanılır;
// ekran çizimi mtx'i uzun tutabilir ve NimBLE görevini bekletmek telefon bağlantısını koparabilir.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// Tarama NimBLE'nin kendisiyle (ble_gap_disc) yapılır; kitaplığın NimBLEScan nesnesi hiç
// oluşturulmaz. Kitaplık nesne yokken bile onHostSync'e bağlantı verdiği için bütün tarama sınıfı
// (yaklaşık 2 KB) yazılıma giriyordu. platformio.ini'deki --wrap bu çağrıyı aşağıdaki boş işleve
// yönlendirir; kitaplık onu yalnız nesne varsa çağırdığından davranış değişmez.
extern "C" void __wrap__ZN10NimBLEScan10onHostSyncEv(void*) {}

static BleDev* bsList = nullptr;
static int bsN = 0;
static uint16_t bsDropped = 0;
static uint32_t bsPackets = 0, bsStartedAt = 0, bsUntil = 0, bsEndedAt = 0;
static volatile bool bsRunning = false;
static bool bsReport = false, bsDone = false, bsWas = false, bsAutoFree = false;
static String bsErr;
static portMUX_TYPE bsMux = portMUX_INITIALIZER_UNLOCKED;

static int bsGap(struct ble_gap_event* e, void*) {
  if (e->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    bsRunning = false;
    return 0;
  }
  if (e->type != BLE_GAP_EVENT_DISC) return 0;
  const ble_gap_disc_desc& r = e->disc;
  BleDev d;
  bleDevInit(d);
  d.addrType = r.addr.type & 1;
  for (int i = 0; i < 6; i++) d.addr[i] = r.addr.val[5 - i];
  d.rssi = r.rssi == 127 ? -127 : r.rssi;
  if (r.event_type <= BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) d.flags |= BD_CONN;
  else if (r.event_type <= BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND) d.flags |= BD_NOCONN;  // tarama yanıtı bunu söylemez
  bleParseAd(r.data, r.length_data, d);
  uint32_t now = millis();
  portENTER_CRITICAL(&bsMux);
  if (bsList) bleMerge(bsList, bsN, BLE_LIST_CAP, d, now, bsDropped);
  bsPackets++;
  portEXIT_CRITICAL(&bsMux);
  return 0;
}

static String bsError() { return bsErr; }
static bool bsBusy() { return bsRunning; }

static bool bsStart(uint32_t ms, bool report) {
  if (bsRunning) {
    bsErr = "Bluetooth taraması zaten sürüyor";
    return false;
  }
  if (!bleServer || !ble_hs_synced()) {
    bsErr = "Bluetooth hazır değil";
    return false;
  }
  if (!bsList) {
    const size_t need = sizeof(BleDev) * BLE_LIST_CAP;
    BleDev* l;
    String why;
    l = memAsk(MJ_BLESCAN, need, need, &why) ? (BleDev*)calloc(BLE_LIST_CAP, sizeof(BleDev)) : nullptr;  // os/membudget.h
    if (!l) {
      bsErr = why.length() ? why : String("Bellek yetmiyor");
      return false;
    }
    portENTER_CRITICAL(&bsMux);
    bsList = l;
    portEXIT_CRITICAL(&bsMux);
  }
  portENTER_CRITICAL(&bsMux);
  bsN = 0;
  bsDropped = 0;
  bsPackets = 0;
  portEXIT_CRITICAL(&bsMux);
  ble_gap_disc_params p = {};
  p.itvl = 160;                       // 100 ms
  p.window = bleConnected ? 32 : 80;  // telefon bağlıyken 20 ms, değilse 50 ms dinlenir
  p.passive = 0;
  p.filter_duplicates = 0;            // sinyal gücü canlı güncellensin
  int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, ms, &p, bsGap, nullptr);
  char b[64];
  if (rc) {
    snprintf(b, sizeof b, "Tarama başlatılamadı (NimBLE %d)", rc);
    bsErr = b;
    return false;
  }
  bsReport = bsAutoFree = report;  // Terminal'in listesi kendiliğinden bırakılır, Ağ'ınki ekranla
  bsDone = false;
  bsRunning = bsWas = true;
  bsStartedAt = millis();
  bsUntil = bsStartedAt + ms + 1500;
  snprintf(b, sizeof b, "tarama başladı (%u sn%s)", (unsigned)(ms / 1000), bleConnected ? ", telefon bağlı" : "");
  sysLog("bt", b);
  return true;
}

static void bsStop() {
  if (bsRunning) ble_gap_disc_cancel();
  bsRunning = false;
}

static void bsFree() {
  bsStop();
  portENTER_CRITICAL(&bsMux);
  BleDev* l = bsList;
  bsList = nullptr;
  bsN = 0;
  portEXIT_CRITICAL(&bsMux);
  free(l);
  bsReport = bsDone = false;
}

static int bsCount() { return bsN; }

static int bsGet(int first, BleDev* out, int max) {
  uint8_t idx[BLE_LIST_CAP];
  int got = 0;
  portENTER_CRITICAL(&bsMux);
  if (bsList) {
    bleOrder(bsList, bsN, idx);
    for (int i = first; i >= 0 && i < bsN && got < max; i++) out[got++] = bsList[idx[i]];
  }
  portEXIT_CRITICAL(&bsMux);
  return got;
}

static bool bsFind(const uint8_t* addr, uint8_t type, BleDev& out) {
  bool ok = false;
  portENTER_CRITICAL(&bsMux);
  for (int i = 0; bsList && i < bsN && !ok; i++)
    if (bsList[i].addrType == type && !memcmp(bsList[i].addr, addr, 6)) {
      out = bsList[i];
      ok = true;
    }
  portEXIT_CRITICAL(&bsMux);
  return ok;
}

static void bsStats(uint32_t& packets, uint16_t& dropped) {
  portENTER_CRITICAL(&bsMux);
  packets = bsPackets;
  dropped = bsDropped;
  portEXIT_CRITICAL(&bsMux);
}

static bool bsTakeDone() {
  if (!bsDone) return false;
  bsDone = false;
  return true;
}

static void bsRelease(bool force) {
  if (bsReport && !force) return;  // Terminal'den başlatıldı: bitince rapor edilecek
  bsFree();
}

// Saat servisinden: biten taramayı işaretler; Terminal taramasının listesi 5 dk sonra bırakılır
static void bsPoll() {
  if (bsRunning && (int32_t)(millis() - bsUntil) > 0) {  // tamamlanma olayı gelmediyse
    ble_gap_disc_cancel();
    bsRunning = false;
  }
  if (bsWas && !bsRunning) {
    bsWas = false;
    bsEndedAt = millis();
    char b[48];
    snprintf(b, sizeof b, "tarama bitti: %d cihaz, %u paket", bsN, (unsigned)bsPackets);
    sysLog("bt", b);
    if (bsReport) bsDone = true;
    bsReport = false;
  }
  if (bsAutoFree && !bsRunning && bsList && !bsDone && millis() - bsEndedAt > 300000) bsFree();
}

static void bsInfo(std::vector<String>& o) {
  if (bleLinked) blePush(o, "Telefon: bağlı · MTU %u", (unsigned)bleMtu);
  else o.push_back(bleHasToken ? "Telefon: eşleşmiş, bağlı değil" : "Telefon: eşleşme yok");
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  blePush(o, "Kartın adresi: %s · ad " BLE_NAME, macText(mac).c_str());
  o.push_back(NimBLEDevice::getAdvertising()->isAdvertising() ? "Reklam: yayında" : "Reklam: kapalı");
  if (bsRunning) blePush(o, "Tarama: sürüyor · %d cihaz, %u paket", bsN, (unsigned)bsPackets);
  else if (bsList) blePush(o, "Son tarama: %d cihaz, %u paket · %u sn önce", bsN, (unsigned)bsPackets, (unsigned)((millis() - bsEndedAt) / 1000));
  else o.push_back("Tarama: yok (bluetooth scan)");
}
