#pragma once
// DAXEN VANE sistem katmanı · Varlık paketinin indirilmesi (yazı tipleri, logo, Lua dosyaları;
// okuma ui/font.h'de). Paket eksik ya da yazılımın beklediği sürüm değilse kart onu çalıştığı
// sürümün GitHub sayfasından indirir: açılışta (Bluetooth başlamadan, bellek bolken) ve sonra Wi-Fi
// bağlıyken dakikada bir. Bağlantı sertifika doğrulamalı (güncellemenin kökleri); önce geçici
// dosyaya yazılır, boy, sürüm ve CRC32 tutarsa eskisinin yerine geçer.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.
#define ASSET_TMP "/daxen/sys/varlik.tmp"

static bool assetsDownload(void (*prog)(int pct)) {
  if (!sdOk || WiFi.status() != WL_CONNECTED || FW_BUILD == 0) return false;
  if (!SD.exists("/daxen")) SD.mkdir("/daxen");
  if (!SD.exists("/daxen/sys")) SD.mkdir("/daxen/sys");
  String url = "https://github.com/" GITHUB_REPO "/releases/download/fw-v" + String(FW_BUILD) + "/varlik.bin";
  WiFiClientSecure c;
  c.setCACertBundle(OTA_ROOTS);
  HTTPClient h;
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.setTimeout(15000);
  if (!h.begin(c, url)) return false;
  int code = h.GET();
  if (code != 200 || h.getSize() != (int)ASSET_LEN) {
    sysLog("varlık", "indirilemedi: HTTP " + String(code));
    h.end();
    return false;
  }
  File f = SD.open(ASSET_TMP, FILE_WRITE);
  if (!f) {
    h.end();
    return false;
  }
  WiFiClient* st = h.getStreamPtr();
  uint8_t b[512], head[8] = {0};
  uint32_t got = 0, crc = 0, last = millis();
  while (got < ASSET_LEN && millis() - last < 15000) {
    int n = st->available() ? st->read(b, std::min<uint32_t>(sizeof b, ASSET_LEN - got)) : 0;
    if (n <= 0) {
      delay(5);
      continue;
    }
    last = millis();
    if (f.write(b, n) != (size_t)n) break;
    for (int i = 0; i < n && got + i < 8; i++) head[got + i] = b[i];
    uint32_t s0 = got >= 8 ? 0 : std::min<uint32_t>(n, 8 - got);
    crc = crc32Upd(crc, b + s0, n - s0);
    got += n;
    if (prog) prog(got * 100 / ASSET_LEN);
  }
  f.close();
  h.end();
  if (got != ASSET_LEN || memcmp(head, "DXV1", 4) || rd32(head + 4) != ASSET_VER || crc != ASSET_VER) {
    SD.remove(ASSET_TMP);
    sysLog("varlık", "indirilen paket eksik ya da bozuk");
    return false;
  }
  {
    PackLock l;
    if (packF) packF.close();
    packOk = false;
    SD.remove(ASSET_PATH);
    SD.rename(ASSET_TMP, ASSET_PATH);
  }
  bool ok = packOpen();
  sysLog("varlık", ok ? "paket indirildi" : "paket açılamadı");
  if (ok) assetsFresh = true;
  return ok;
}

// Açılışta: paket yoksa ve Wi-Fi bağlıysa hemen indirilir (açılış ekranında ilerlemeyle)
static void assetsBoot() {
  if (packOk || !sdOk || WiFi.status() != WL_CONNECTED) return;
  uiSplash("Yazı tipleri indiriliyor…");
  assetsDownload([](int p) {
    static int last = -1;
    if (p / 10 == last) return;
    last = p / 10;
    uiSplash("Yazı tipleri indiriliyor: %" + String(p));
  });
  assetsFresh = false;  // açılış ekranı zaten baştan çizilecek
}
