#pragma once
// DAXEN VANE sistem katmanı · Kurtarma kipi. Açılışta (logo ekranında) BOOT 3 sn basılı tutulunca
// ya da art arda 3 çökmeden sonra açılır; Wi-Fi, bulut ve Lua başlamadan önce. Seçim: satıra dokun
// ya da BOOT'a kısa bas (sonraki), uzun bas (seç). Silen ya da değiştiren seçenekler ikinci kez
// seçilince uygulanır. SD karttaki dosyalara hiçbir seçenek dokunmaz.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.
#include <nvs_flash.h>

RTC_NOINIT_ATTR static uint32_t rcMagic, rcCrashes;  // yazılımla yeniden başlamada korunur
static bool rcSafe = false;                           // güvenli başlatma: bu açılışta Wi-Fi kapalı

// Açılışta bir kez: çökmeyle (panik, bekçi) başladıysa sayar; true: art arda 3 çökme
static bool rcCountCrash() {
  esp_reset_reason_t r = esp_reset_reason();
  bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
  if (rcMagic != 0x52435631) rcMagic = 0x52435631, rcCrashes = 0;
  rcCrashes = crash ? rcCrashes + 1 : 0;
  return rcCrashes >= 3;
}
static void rcHealthy() { rcCrashes = 0; }  // 45 sn sorunsuz çalıştı

static const char* const RC_OPT[] = {"Normal başlat", "Güvenli başlat (Wi-Fi kapalı)", "PIN'i ve ekran kilidini kaldır",
                                     "Dokunmatik ayarını sıfırla", "Önceki yazılıma dön", "Bütün ayarları sıfırla"};
static const int RC_N = 6, RC_Y = 50, RC_H = 28;

static void rcDraw(int sel, int armed, const String& note, const String& why) {
  fillBg(0, 0, SCR_W, SCR_H);
  text("Kurtarma kipi", 12, 8, F_MD, C_TEXT, C_BG);
  text(why + " · fw-" + String(FW_BUILD), 12, 32, F_XS, C_TEXT3, C_BG);
  for (int i = 0; i < RC_N; i++) {
    int y = RC_Y + i * RC_H;
    bool s = i == sel;
    uint16_t bg = s ? C_ACCBG : C_BG;
    panel(8, y, 304, RC_H - 2, bg, s ? C_ACC : C_LINE);
    text(fit(i == armed ? String("Uygulamak için yeniden seç") : String(RC_OPT[i]), F_SM, 290), 16, y + 13, F_SM, i == armed ? C_ACC : (s ? C_TEXT : C_TEXT2), bg,
         ML_DATUM);
  }
  text(note.length() ? note : String("Dokun ya da BOOT: kısa bas sonraki, uzun bas seç"), 12, 226, F_XS, note.length() ? C_ACC : C_TEXT3, C_BG);
}

// true: kurtarma kipinden çık ve açılışa devam et
static bool rcDo(int i, String& note) {
  switch (i) {
    case 0: return true;
    case 1: rcSafe = true; return true;
    case 2:
      authClearPin();
      cfg.lockOn = false;
      saveConfig();
      note = "PIN ve ekran kilidi kaldırıldı";
      return false;
    case 3:
      cfg.calibrated = false;
      saveConfig();
      note = "Dokunmatik ayarı açılışta yeniden yapılacak";
      return false;
    case 4: {
      const esp_partition_t* o = esp_ota_get_next_update_partition(nullptr);
      esp_app_desc_t d;
      if (!o || esp_ota_get_partition_description(o, &d) != ESP_OK || esp_ota_set_boot_partition(o) != ESP_OK) {
        note = "Önceki yazılım yok ya da bozuk";
        return false;
      }
      sysLog("kurtarma", "önceki yazılıma dönülüyor");
      text("Önceki yazılım açılıyor…", 12, 226, F_XS, C_ACC, C_BG);
      delay(600);
      ESP.restart();
      return false;
    }
    case 5:
      sysLog("kurtarma", "bütün ayarlar sıfırlanıyor");
      {
        Lock l;
        prefs.end();
        nvs_flash_erase();
        nvs_flash_init();
      }
      text("Ayarlar silindi, yeniden başlıyor…", 12, 226, F_XS, C_ACC, C_BG);
      delay(800);
      ESP.restart();
      return false;
  }
  return true;
}

static void rcRun(const String& why) {
  sysLog("kurtarma", why);
  int sel = 0, armed = -1;
  String note;
  rcDraw(sel, armed, note, why);
  uint32_t bootAt = 0;
  bool wasTouch = false;
  for (;;) {
    int x, y, pick = -1;
    bool t = getTouch(x, y);
    if (t && !wasTouch && y >= RC_Y && y < RC_Y + RC_N * RC_H) pick = sel = (y - RC_Y) / RC_H;
    wasTouch = t;
    bool b = digitalRead(PIN_BOOT) == LOW;
    if (b && !bootAt) bootAt = millis();
    if (!b && bootAt) {
      bool lng = millis() - bootAt > 700;
      bootAt = 0;
      if (lng) pick = sel;
      else {
        sel = (sel + 1) % RC_N;
        armed = -1;
        rcDraw(sel, armed, note = "", why);
      }
    }
    if (pick >= 0) {
      if (pick >= 2 && armed != pick) {
        armed = pick;
        note = "";
      } else {
        armed = -1;
        if (rcDo(pick, note)) {
          fillBg(0, 0, SCR_W, SCR_H);
          return;
        }
      }
      rcDraw(sel, armed, note, why);
    }
    delay(15);
  }
}
