#pragma once
// DAXEN VANE sistem katmanı · SD karttan güncelleme.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// SD karttan güncelleme (açılışta)
// ============================================================================
// Kartın kökündeki update.bin, firmware.bin ya da firmware-<model>.bin dosyası yazılımın
// içindeki "CSFW:<model>:<sürüm>;" işaretiyle tanınır. Yalnızca aynı modelin daha yeni sürümü
// yüklenir; işlenen dosyanın adı değiştirilir ki her açılışta tekrar denenmesin:
//   .yuklendi  yüklendi        .eski  aynı ya da eski sürüm (işaretsiz eski dosyalar da)
//   .bad       başka model ya da uygulama dosyası değil (ör. ilk-kurulum dosyası)
static bool sdFindMarker(File& f, String& variant, int& build) {
  static const char pat[] = "CSFW:";
  uint8_t buf[512];
  int m = 0;
  bool collecting = false;
  String body;
  size_t r;
  while ((r = f.read(buf, sizeof buf)) > 0) {
    for (size_t i = 0; i < r; i++) {
      char ch = (char)buf[i];
      if (collecting) {
        if (ch == ';') {
          int c = body.lastIndexOf(':');
          if (c > 0 && c < (int)body.length() - 1) {
            variant = body.substring(0, c);
            build = body.substring(c + 1).toInt();
            bool digits = true;
            for (size_t k = c + 1; k < body.length(); k++)
              if (!isdigit((unsigned char)body[k])) digits = false;
            if (digits) return true;
          }
          collecting = false;
        } else if ((isalnum((unsigned char)ch) || ch == '_' || ch == ':') && body.length() < 40) {
          body += ch;
          continue;
        } else {
          collecting = false;
        }
        m = 0;
      }
      if (ch == pat[m]) {
        if (++m == 5) {
          collecting = true;
          body = "";
          m = 0;
        }
      } else {
        m = ch == pat[0] ? 1 : 0;
      }
    }
  }
  return false;
}

static void sdRenameDone(const char* name, const char* suffix) {
  String to = String(name) + suffix;
  if (SD.exists(to)) SD.remove(to);
  if (!SD.rename(String(name), to)) SD.remove(name);
}

static void sdTryUpdate() {
  if (!sdOk) return;
  static const char* names[] = {"/update.bin", "/firmware.bin", "/firmware-" FW_VARIANT ".bin", SD_DIR "/update.bin"};
  for (const char* name : names) {
    if (!SD.exists(name)) continue;
    File f = SD.open(name, FILE_READ);
    if (!f) continue;
    size_t size = f.size();
    int magic = f.read();
    String variant;
    int build = -1;
    bool marked = magic == 0xE9 && size > 100000 && sdFindMarker(f, variant, build);
    f.close();
    if (magic != 0xE9 || (marked && variant != FW_VARIANT)) {
      sdRenameDone(name, ".bad");
      continue;
    }
    if (!marked || build <= FW_BUILD) {
      sdRenameDone(name, ".eski");
      continue;
    }
    f = SD.open(name, FILE_READ);
    if (!f) continue;
    esp_ota_mark_app_valid_cancel_rollback();
    otaProgress = 0;
    uiOtaProgress();
    bool ok = Update.begin(size, U_FLASH);
    if (ok) {
      Update.onProgress([](size_t cur, size_t total) {
        if (total) otaProgress = (int)((uint64_t)cur * 100 / total);
        uiOtaProgress();
      });
      ok = Update.writeStream(f) == size && Update.end(true);
    }
    f.close();
    if (ok) {
      sdRenameDone(name, ".yuklendi");
      otaProgress = 100;
      uiOtaProgress();
      delay(800);
      ESP.restart();
    }
    Update.abort();
    otaProgress = -1;
    sdRenameDone(name, ".bad");
    fillBg(0, 0, SCR_W, SCR_H);
    text("SD'den güncelleme olmadı", 160, 100, F_MD, C_TEXT, C_BG, MC_DATUM);
    text("Dosya bozuk olabilir; eski sürümle devam", 160, 128, F_SM, C_TEXT2, C_BG, MC_DATUM);
    delay(3000);
    return;
  }
}
