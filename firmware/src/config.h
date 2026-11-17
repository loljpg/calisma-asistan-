#pragma once
#include <Arduino.h>

// ---------- Sürüm ----------
#ifndef FW_BUILD
#define FW_BUILD 0  // GitHub Actions derlerken çalıştırma numarasını yazar
#endif

#ifdef FW_VARIANT_ST7789
#define FW_VARIANT "cyd_st7789"
#define DEFAULT_INVERT true
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
#define TOUCH_CS 33

// ---------- Ekran ----------
#define SCREEN_W 320
#define SCREEN_H 240

// RGB565 renkleri
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define COL_BG RGB(10, 15, 36)
#define COL_CARD RGB(27, 34, 70)
#define COL_CARD_ON RGB(54, 64, 122)
#define COL_LINE RGB(46, 55, 102)
#define COL_HI RGB(74, 85, 144)
#define COL_TEXT RGB(255, 255, 255)
#define COL_MUTED RGB(182, 188, 208)
#define COL_ACCENT RGB(125, 211, 252)
#define COL_ON_ACCENT RGB(11, 20, 38)
#define COL_OK RGB(52, 211, 153)
#define COL_WARN RGB(253, 186, 116)
#define COL_ERR RGB(248, 113, 113)

// ---------- Zamanlama ----------
#define VIEW_EVERY_MS 30000UL
#define STATUS_EVERY_MS 60000UL
#define WEATHER_EVERY_MS (15UL * 60UL * 1000UL)
#define OTA_EVERY_MS (12UL * 60UL * 60UL * 1000UL)
#define POMODORO_WORK_SEC (25 * 60)
