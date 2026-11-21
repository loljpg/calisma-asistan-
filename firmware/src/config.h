#pragma once
#include <Arduino.h>

// ---------- Sürüm ----------
#ifndef FW_BUILD
#define FW_BUILD 0  // GitHub Actions derlerken çalıştırma numarasını yazar
#endif

#ifdef FW_VARIANT_ST7789
#define FW_VARIANT "cyd_st7789"
#define DEFAULT_INVERT false  // iki USB'li kartta doğru renkler ters çevirmeden geliyor
#else
#define FW_VARIANT "cyd"
#define DEFAULT_INVERT false
#endif

#define GITHUB_REPO "loljpg/calisma-asistan-"

// ---------- Hava durumu (Sakarya) ----------
#define WEATHER_CITY "Sakarya"
#define WEATHER_LAT "40.78"
#define WEATHER_LON "30.40"

// ---------- CYD pinleri ----------
#define PIN_BL 21          // ekran arka ışığı
#define PIN_LED_R 4        // RGB LED (LOW = yanar)
#define PIN_LED_G 16
#define PIN_LED_B 17
#define PIN_LDR 34         // ışık sensörü
#define PIN_SPK 26         // SPEAK soketi (kart üzerindeki yükselteç); ses çıkışı yok, açılışta LOW tutulur
#define PIN_BOOT 0         // BOOT butonu

#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25
#define TOUCH_CS 33        // dokunmatik yazılımsal SPI ile okunur; VSPI SD kartın

#define SD_SCK 18          // SD kart yuvası (VSPI)
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS 5

// ---------- Ekran ----------
#define SCREEN_W 320
#define SCREEN_H 240

// RGB565 (kırparak; tasarım renkleri için ui/theme.h rgbn yuvarlar)
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// DAXEN VANE renkleri ui/theme.h'de (tek yerden yönetilir).
#define NIGHT_FROM 22  // otomatik temada gece başlangıcı (saat)
#define NIGHT_TO 7

// ---------- Zamanlama ----------
#define VIEW_EVERY_MS 30000UL
#define STATUS_EVERY_MS 60000UL
#define WEATHER_EVERY_MS (15UL * 60UL * 1000UL)
#define POMODORO_WORK_SEC (25 * 60)
#define TIMER_POLL_MS 10000UL      // canlı bağlantı yokken ortak kronometre bu aralıkla okunur
#define VIEW_POLL_LIVE_MS (5UL * 60UL * 1000UL)  // canlı bağlantı varken plan yine de ara sıra okunur
#define LONG_PRESS_MS 650

// ---------- Bluetooth (docs/bluetooth.md) ----------
#define BLE_NAME "CalismaSaati"
#define BLE_SVC "6e3a0001-5a17-4c2d-9b8e-0c41a5a7c0de"
#define BLE_RX "6e3a0002-5a17-4c2d-9b8e-0c41a5a7c0de"   // telefon yazar
#define BLE_TX "6e3a0003-5a17-4c2d-9b8e-0c41a5a7c0de"   // masa saati bildirir
#define BLE_PAIR_MS (2UL * 60UL * 1000UL)              // Ayarlar → Bluetooth: eşleşme kipi süresi
#define BANNER_MS 5000                                  // gelen bildirim ekranda bu kadar kalır
#define ICON_PX 20                                      // bildirim simgesi (20×20)
#define ART_PX 80                                       // şarkı kapağı (80×80)
