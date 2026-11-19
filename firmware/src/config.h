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
#define PORTAL_NAME "CalismaSaati-Kurulum"

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

// RGB565 renkleri
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// "Sentez" tasarımı: gündüz açık kâğıt, gece koyu. Renkler main.cpp'deki paletlerden gelir.
struct Palette {
  uint16_t bg, grid, surface, line, lineStrong, text, muted, faint, red, redSoft, redLine;
  uint16_t ink, onInk, inkMuted, ink2, inkLine, track, ok, cloud;
};
extern const Palette* th;

#define COL_BG (th->bg)
#define COL_GRID (th->grid)
#define COL_SURFACE (th->surface)
#define COL_LINE (th->line)
#define COL_LINE_STRONG (th->lineStrong)
#define COL_TEXT (th->text)
#define COL_MUTED (th->muted)
#define COL_FAINT (th->faint)
#define COL_RED (th->red)
#define COL_RED_SOFT (th->redSoft)
#define COL_RED_LINE (th->redLine)
#define COL_INK (th->ink)
#define COL_ON_INK (th->onInk)
#define COL_INK_MUTED (th->inkMuted)
#define COL_INK2 (th->ink2)
#define COL_INK_LINE (th->inkLine)
#define COL_TRACK (th->track)
#define COL_OK (th->ok)
#define COL_CLOUD (th->cloud)
#define INK_TEXT RGB(22, 23, 26)  // beyaz düğme üstündeki yazı

#define GRID_STEP 22
#define NIGHT_FROM 22  // otomatik temada gece başlangıcı (saat)
#define NIGHT_TO 7

// ---------- Zamanlama ----------
#define VIEW_EVERY_MS 30000UL
#define STATUS_EVERY_MS 60000UL
#define WEATHER_EVERY_MS (15UL * 60UL * 1000UL)
#define OTA_EVERY_MS (12UL * 60UL * 60UL * 1000UL)
#define POMODORO_WORK_SEC (25 * 60)
#define TIMER_POLL_MS 10000UL      // canlı bağlantı yokken ortak kronometre bu aralıkla okunur
#define VIEW_POLL_LIVE_MS (5UL * 60UL * 1000UL)  // canlı bağlantı varken plan yine de ara sıra okunur
#define LONG_PRESS_MS 650
