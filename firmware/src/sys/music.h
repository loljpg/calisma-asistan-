#pragma once
// DAXEN VANE sistem katmanı · Müzik modülü: indirme, bölüme yazma, açma.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// Müzik yazılımı (kulaklıkla SD müzik): indirme, bölüme yazma, açma
// ============================================================================
// Müzik ayrı bir yazılımdır (klasik Bluetooth ve MP3 çözücü bu yazılıma sığmıyor). SD karttaki
// /daxen/sys/muzik-<model>.bin boştaki güncelleme bölümüne yazılır ve kart oradan başlar. Müzik
// yazılımından çıkınca ya da bir sorun olursa kart kendiliğinden bu yazılıma döner.
#define MUSIC_DIR "/daxen/sys"
#define MUSIC_FILE MUSIC_DIR "/muzik-" FW_VARIANT ".bin"
#define MUSIC_MIN_BUILD 28  // hoparlör çıkışı bu sürümle geldi; daha eski modül yenisiyle değiştirilir
static String musicErr;
static int musicCachedState = -1;
static uint32_t musicCachedSize = 0;

// Dosyadaki "DXMU:<model>:<sürüm>;" işaretini arar
static bool musicReadMarker(File& f, String& variant, int& build) {
  static const char key[] = "DXMU:";
  uint8_t buf[512];
  size_t kl = 5, match = 0;
  String tail;
  bool inMarker = false;
  f.seek(0);
  while (f.available()) {
    int n = f.read(buf, sizeof buf);
    for (int i = 0; i < n; i++) {
      char c = buf[i];
      if (inMarker) {
        if (c == ';') {
          int k = tail.indexOf(':');
          if (k > 0) {
            variant = tail.substring(0, k);
            build = tail.substring(k + 1).toInt();
            return true;
          }
          inMarker = false;
          tail = "";
        } else if (tail.length() < 40 && c >= 32 && c < 127) {
          tail += c;
        } else {
          inMarker = false;
          tail = "";
        }
        continue;
      }
      if (c == key[match]) {
        if (++match == kl) {
          inMarker = true;
          match = 0;
        }
      } else {
        match = c == key[0] ? 1 : 0;
      }
    }
  }
  return false;
}

// Kökteki muzik-<model>.bin (elle kopyalanmış) sistem klasörüne taşınır
static void musicAdoptRootFile() {
  const char* root = "/muzik-" FW_VARIANT ".bin";
  if (!sdOk || webUpFile || !SD.exists(root)) return;
  if (!SD.exists("/daxen")) SD.mkdir("/daxen");
  if (!SD.exists(MUSIC_DIR)) SD.mkdir(MUSIC_DIR);
  if (SD.exists(MUSIC_FILE)) SD.remove(MUSIC_FILE);
  SD.rename(root, MUSIC_FILE);
}

static int musicState() {
  if (!sdOk) return 2;
  musicAdoptRootFile();
  File f = SD.open(MUSIC_FILE);
  if (!f) return 1;
  uint32_t sz = f.size();
  if (musicCachedState >= 0 && sz == musicCachedSize) {
    f.close();
    return musicCachedState;
  }
  String v;
  int b = 0;
  bool okMarker = musicReadMarker(f, v, b) && v == FW_VARIANT;
  f.close();
  musicCachedSize = sz;
  musicCachedState = !okMarker ? 1 : (b >= MUSIC_MIN_BUILD ? 0 : 3);  // 3: eski modül
  return musicCachedState;
}

static String musicInfo() {
  if (musicErr.length()) return musicErr;
  switch (musicState()) {
    case 0: return "Müzik modülü hazır";
    case 2: return "SD kart takılı değil";
    case 3: return "Müzik modülü eski: yenisi indirilecek (1,3 MB)";
    default: return "Müzik modülü SD kartta yok (1,3 MB indirilecek)";
  }
}

static void musicDownload() {
  {
    Lock l;
    prefs.begin("cs", false);
    prefs.putBool("getmus", true);
    prefs.end();
  }
  uiSplash("Müzik modülü indirilecek…");
  delay(600);
  esp_ota_mark_app_valid_cancel_rollback();
  ESP.restart();
}

static void musicProgress(const String& title, int pct) {
  static int last = -2;
  if (pct < 0 || last < 0 || pct < last) {
    splashLogo();
    text(title, 160, 172, F_SM, C_TEXT, C_BG, MC_DATUM);
    text("Kapatma", 160, 222, F_XS, C_TEXT3, C_BG, MC_DATUM);
  }
  last = pct;
  progress(60, 190, 200, 2, std::max(0, pct), C_ACC);
  fillBg(60, 198, 200, 16);
  if (pct >= 0) text("%" + String(pct), 160, 205, F_XS, C_TEXT2, C_BG, MC_DATUM);
}

// Müzik yazılımını boştaki bölüme yazar (gerekirse) ve oradan başlar.
static void musicLaunch() {
  if (musicState() != 0) return;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next) {
    musicErr = "Güncelleme bölümü bulunamadı";
    return;
  }
  esp_ota_mark_app_valid_cancel_rollback();  // dönüş için bu yazılım "sağlam" olmalı
  uint8_t sha[32], saved[32];
  size_t n;
  String lbl;
  {
    Lock l;
    prefs.begin("mus2", true);
    n = prefs.getBytes("sha", saved, 32);
    lbl = prefs.getString("lbl", "");
    prefs.end();
  }
  bool same = n == 32 && lbl == next->label && esp_partition_get_sha256(next, sha) == ESP_OK && !memcmp(sha, saved, 32);
  if (same) {
    musicProgress("Müzik açılıyor", 100);
    if (esp_ota_set_boot_partition(next) == ESP_OK) {
      delay(200);
      ESP.restart();
    }
  }
  File f = SD.open(MUSIC_FILE);
  if (!f) {
    musicErr = "Müzik dosyası açılamadı";
    return;
  }
  size_t size = f.size();
  musicProgress("Müzik modülü yükleniyor", 0);
  if (!Update.begin(size)) {
    f.close();
    musicErr = "Yazılamadı: " + String(Update.errorString());
    return;
  }
  uint8_t buf[2048];
  size_t done = 0;
  int lastPct = -1;
  while (done < size) {
    int r = f.read(buf, sizeof buf);
    if (r <= 0 || Update.write(buf, r) != (size_t)r) break;
    done += r;
    int pct = done * 100 / size;
    if (pct != lastPct) {
      lastPct = pct;
      musicProgress("Müzik modülü yükleniyor", pct);
    }
  }
  f.close();
  if (done != size || !Update.end(true)) {
    musicErr = "Yükleme yarıda kaldı: " + String(Update.errorString());
    Update.abort();
    return;
  }
  if (esp_partition_get_sha256(next, sha) == ESP_OK) {
    Lock l;
    prefs.begin("mus2", false);
    prefs.putBytes("sha", sha, 32);
    prefs.putString("lbl", next->label);
    prefs.end();
  }
  delay(200);
  ESP.restart();
}

// Açılışta (Bluetooth başlamadan, bellek bolken): istenmişse modülü indirip açar.
// İndirme ayrı bir görevde (16 KB yığın) yapılır: TLS el sıkışması kurulum görevinin 8 KB
// yığınını zorluyordu. Başlarken NVS'ye "mdl" yazılır; kart indirme sırasında yeniden başlarsa
// bir sonraki açılışta sebebi (voltaj düşmesi / çökme) Müzik ekranında gösterilir.
static volatile bool musicDlDone = false;
static void musicDlTask(void*);

static void musicBootTasks() {
  bool want, crashed;
  {
    Lock l;
    prefs.begin("cs", false);
    want = prefs.getBool("getmus", false);
    if (want) prefs.remove("getmus");
    crashed = prefs.getBool("mdl", false);
    if (crashed) prefs.remove("mdl");
    prefs.end();
  }
  if (crashed && !want) {
    esp_reset_reason_t r = esp_reset_reason();
    musicErr = r == ESP_RST_BROWNOUT ? "İndirirken voltaj düştü: hoparlörü çıkar, sağlam kablo/powerbank dene"
               : (r == ESP_RST_PANIC || r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT)
                   ? "İndirirken kart çöktü, tekrar dene"
                   : "İndirme yarıda kesildi, tekrar dene";
  }
  if (!want || !sdOk) return;
  // Zayıf sinyalde ilk bağlantı uzun sürebilir: 45 sn beklenir, arada kayıtlı ağlar taranıp
  // görünenlerin en güçlüsü yeniden denenir. Ekranda geçen süre görünür.
  uint32_t t0 = millis(), lastDraw = 0;
  int tries = 0;
  if (WiFi.status() != WL_CONNECTED) musicProgress("Müzik modülü için Wi-Fi bekleniyor", -1);
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 45000) {
    uint32_t el = millis() - t0;
    if ((tries == 0 && el > 10000) || (tries == 1 && el > 26000)) {
      tries++;
      wifiAutoPick();
    }
    if (millis() - lastDraw >= 1000) {
      lastDraw = millis();
      fillBg(60, 198, 200, 16);
      text(String((millis() - t0) / 1000) + " sn", 160, 205, F_XS, C_TEXT2, C_BG, MC_DATUM);
    }
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    int st = WiFi.status();
    musicErr = st == WL_NO_SSID_AVAIL   ? "İndirilemedi: kayıtlı Wi-Fi ağı bulunamadı"
               : st == WL_CONNECT_FAILED ? "İndirilemedi: Wi-Fi şifresi kabul edilmedi"
                                         : "İndirilemedi: Wi-Fi'ye bağlanılamadı (sinyal zayıf)";
    return;
  }
  {
    Lock l;
    prefs.begin("cs", false);
    prefs.putBool("mdl", true);
    prefs.end();
  }
  musicDlDone = false;
  if (xTaskCreatePinnedToCore(musicDlTask, "mdl", 16384, nullptr, 1, nullptr, 1) == pdPASS) {
    while (!musicDlDone) delay(50);
  } else {
    musicErr = "İndirilemedi: bellek yetmedi";
  }
  {
    Lock l;
    prefs.begin("cs", false);
    prefs.remove("mdl");
    prefs.end();
  }
  if (musicErr.length()) return;
  musicLaunch();
}

static void musicDlTask(void*) {
  []() {
    if (!SD.exists("/daxen")) SD.mkdir("/daxen");
    if (!SD.exists(MUSIC_DIR)) SD.mkdir(MUSIC_DIR);
    String url = "https://github.com/" GITHUB_REPO "/releases/download/fw-v" + String(FW_BUILD) + "/muzik-" FW_VARIANT ".bin";
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    musicProgress("Müzik modülü indiriliyor", 0);
    int code = -1;
    if (http.begin(client, url)) {
      http.setUserAgent("daxen-vane");
      code = http.GET();
    }
    if (code != 200) {
      http.end();
      musicErr = "İndirilemedi (" + String(code) + ")";
      return;
    }
    int total = http.getSize();
    const char* tmp = MUSIC_DIR "/muzik.tmp";
    File out = SD.open(tmp, FILE_WRITE);
    WiFiClient* s = http.getStreamPtr();
    static uint8_t buf[2048];
    int got = 0, lastPct = -1;
    uint32_t lastData = millis();
    while (http.connected() && (total < 0 || got < total) && millis() - lastData < 20000) {
      size_t a = s->available();
      if (!a) {
        delay(5);
        continue;
      }
      int r = s->readBytes(buf, std::min(a, sizeof buf));
      if (r > 0) {
        out.write(buf, r);
        got += r;
        lastData = millis();
        int pct = total > 0 ? (int)((int64_t)got * 100 / total) : 0;
        if (pct != lastPct) {
          lastPct = pct;
          musicProgress("Müzik modülü indiriliyor", pct);
        }
      }
    }
    out.close();
    http.end();
    if (total > 0 && got != total) {
      SD.remove(tmp);
      musicErr = "İndirme yarıda kaldı";
      return;
    }
    if (SD.exists(MUSIC_FILE)) SD.remove(MUSIC_FILE);
    SD.rename(tmp, MUSIC_FILE);
    musicCachedState = -1;
    musicErr = "";
    if (musicState() != 0) {
      musicErr = "İndirilen dosya bu karta uymuyor";
      return;
    }
  }();
  musicDlDone = true;
  vTaskDelete(nullptr);
}
