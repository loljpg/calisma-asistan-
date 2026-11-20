#pragma once
// Dokunmatik (XPT2046) ve buton. Ana yazılım ile müzik yazılımı ortak kullanır.
#include <Arduino.h>
#include "types.h"

// XPT2046 yazılımsal SPI ile okunur (VSPI SD kartın). Okuma sırası ve ortalama
// XPT2046_Touchscreen kütüphanesiyle aynıdır; eski kalibrasyon geçerli kalır.
struct TouchRaw {
  int x = 0, y = 0, z = 0;
};

static void tsBegin() {
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_CLK, OUTPUT);
  digitalWrite(TOUCH_CLK, LOW);
  pinMode(TOUCH_MOSI, OUTPUT);
  pinMode(TOUCH_MISO, INPUT);
  pinMode(TOUCH_IRQ, INPUT);
}

static uint8_t tsXfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(TOUCH_MOSI, (out >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(TOUCH_CLK, HIGH);
    delayMicroseconds(1);
    in = (in << 1) | (digitalRead(TOUCH_MISO) ? 1 : 0);
    digitalWrite(TOUCH_CLK, LOW);
  }
  return in;
}

static uint16_t tsXfer16(uint16_t out) {
  uint16_t hi = tsXfer(out >> 8);
  return (hi << 8) | tsXfer(out & 0xFF);
}

static int16_t besttwoavg(int16_t x, int16_t y, int16_t z) {
  int16_t da = abs(x - y), db = abs(x - z), dc = abs(z - y);
  if (da <= db && da <= dc) return (x + y) >> 1;
  if (db <= da && db <= dc) return (x + z) >> 1;
  return (y + z) >> 1;
}

static TouchRaw tsRead() {
  static bool awake = false;
  static uint32_t lastAt = 0;
  static TouchRaw last;
  if (millis() - lastAt < 3) return last;
  TouchRaw r;
  // Dokunulmuyorken IRQ yüksektir; bir dokunuş başladıktan sonra parmak kalkana kadar okunur.
  if (!awake && digitalRead(TOUCH_IRQ) == HIGH) {
    last = r;
    return r;
  }
  awake = true;
  int16_t d[6] = {0, 0, 0, 0, 0, 0};
  digitalWrite(TOUCH_CS, LOW);
  tsXfer(0xB1);
  int16_t z1 = tsXfer16(0xC1) >> 3;
  int z = z1 + 4095;
  int16_t z2 = tsXfer16(0x91) >> 3;
  z -= z2;
  if (z >= 400) {
    tsXfer16(0x91);  // ilk ölçüm gürültülü
    d[0] = tsXfer16(0xD1) >> 3;
    d[1] = tsXfer16(0x91) >> 3;
    d[2] = tsXfer16(0xD1) >> 3;
    d[3] = tsXfer16(0x91) >> 3;
  }
  d[4] = tsXfer16(0xD0) >> 3;  // son ölçüm, ardından bekleme kipi (IRQ açık)
  d[5] = tsXfer16(0) >> 3;
  digitalWrite(TOUCH_CS, HIGH);
  lastAt = millis();
  if (z < 400) {
    if (z < 75) awake = false;
    last = r;
    return r;
  }
  r.x = besttwoavg(d[0], d[2], d[4]);
  r.y = besttwoavg(d[1], d[3], d[5]);
  r.z = z;
  last = r;
  return r;
}

static bool tsTouched() { return tsRead().z > 0; }

static bool getTouch(int& x, int& y) {
  TouchRaw p = tsRead();
  if (!p.z) return false;
  int rx = cfg.swapXY ? p.y : p.x, ry = cfg.swapXY ? p.x : p.y;
  int dx = cfg.x2 - cfg.x1, dy = cfg.y2 - cfg.y1;
  if (dx == 0 || dy == 0) return false;
  x = 20 + (long)(rx - cfg.x1) * 280 / dx;
  y = 20 + (long)(ry - cfg.y1) * 200 / dy;
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
  return true;
}

// Dokunuşu bekler; BOOT'a basılırsa ya da 60 sn dokunulmazsa false döner.
static bool waitTap(int& rx, int& ry) {
  while (tsTouched()) delay(10);
  delay(150);
  uint32_t t0 = millis();
  while (!tsTouched()) {
    if (digitalRead(PIN_BOOT) == LOW || millis() - t0 > 60000) {
      while (digitalRead(PIN_BOOT) == LOW) delay(10);
      return false;
    }
    delay(10);
  }
  long sx = 0, sy = 0;
  int n = 0;
  t0 = millis();
  while (millis() - t0 < 1500) {
    TouchRaw p = tsRead();
    if (!p.z) break;
    sx += p.x;
    sy += p.y;
    n++;
    delay(15);
  }
  while (tsTouched()) delay(10);
  if (!n) return false;
  rx = sx / n;
  ry = sy / n;
  return true;
}

