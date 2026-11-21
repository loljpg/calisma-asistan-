#pragma once
// Dokunmatik (XPT2046) ve buton.
#include <Arduino.h>
#include <Preferences.h>
#include "types.h"
#include "calib.h"

// XPT2046 yazılımsal SPI ile okunur (VSPI SD kartın). Okuma sırası XPT2046_Touchscreen
// kütüphanesiyle aynıdır (eski kalibrasyon geçerli kalır); her eksen 5 kez ölçülür, ortancası alınır.
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

// Basınç eşiği: basış 400, bırakış 75 altı (arada kalan hafif temas önceki durumu korur)
static const int TS_Z_PRESS = 400, TS_Z_RELEASE = 75;
static const int TS_N = 5;  // eksen başına ölçüm; ortancası alınır (tek tük sıçramalar elenir)

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
  int a[TS_N], b[TS_N];
  digitalWrite(TOUCH_CS, LOW);
  tsXfer(0xB1);
  int16_t z1 = tsXfer16(0xC1) >> 3;
  int z = z1 + 4095;
  int16_t z2 = tsXfer16(0x91) >> 3;
  z -= z2;
  bool on = z >= TS_Z_PRESS;
  if (on) {
    // XPT2046 ardışık çalışır: her aktarım bir önceki komutun sonucunu getirir.
    tsXfer16(0x91);  // ilk ölçüm gürültülü, atılır
    for (int i = 0; i < TS_N - 1; i++) {
      a[i] = tsXfer16(0xD1) >> 3;
      b[i] = tsXfer16(0x91) >> 3;
    }
  }
  int la = tsXfer16(0xD0) >> 3;  // son ölçüm, ardından bekleme kipi (IRQ açık)
  int lb = tsXfer16(0) >> 3;
  digitalWrite(TOUCH_CS, HIGH);
  lastAt = millis();
  if (!on) {
    if (z < TS_Z_RELEASE) awake = false;
    last = r;
    return r;
  }
  a[TS_N - 1] = la;
  b[TS_N - 1] = lb;
  r.x = calMedian(a, TS_N);
  r.y = calMedian(b, TS_N);
  r.z = z;
  last = r;
  return r;
}

static bool tsTouched() { return tsRead().z > 0; }

// --- Kalibrasyon (core/calib.h): ham → ekran ---
static TouchCal tcal;

static void touchUseLegacy() { tcal = calFromLegacy(cfg.swapXY, cfg.x1, cfg.x2, cfg.y1, cfg.y2); }

// "cs" ad alanı açıkken çağrılır. Afin kayıt yoksa eski iki eksenli kayıt kullanılır.
static void touchLoadCal(Preferences& p) {
  if (p.getBool("caff", false)) {
    tcal.a = p.getFloat("ca", 0);
    tcal.b = p.getFloat("cb", 0);
    tcal.c = p.getFloat("cc", 0);
    tcal.d = p.getFloat("cd", 0);
    tcal.e = p.getFloat("ce", 0);
    tcal.f = p.getFloat("cf", 0);
    tcal.ok = (tcal.a != 0 || tcal.b != 0) && (tcal.d != 0 || tcal.e != 0);
    if (tcal.ok) return;
  }
  touchUseLegacy();
}

static void touchStoreCal(Preferences& p) {
  p.putBool("caff", tcal.ok);
  p.putFloat("ca", tcal.a);
  p.putFloat("cb", tcal.b);
  p.putFloat("cc", tcal.c);
  p.putFloat("cd", tcal.d);
  p.putFloat("ce", tcal.e);
  p.putFloat("cf", tcal.f);
}

static bool touchMap(int rx, int ry, int& x, int& y) {
  if (!tcal.ok) touchUseLegacy();
  if (!tcal.ok) return false;
  float fx, fy;
  calApply(tcal, rx, ry, fx, fy);
  x = constrain((int)lroundf(fx), 0, SCREEN_W - 1);
  y = constrain((int)lroundf(fy), 0, SCREEN_H - 1);
  return true;
}

static bool getTouch(int& x, int& y) {
  TouchRaw p = tsRead();
  if (!p.z) return false;
  return touchMap(p.x, p.y, x, y);
}

// Kalibrasyon için tek nokta: basışı bekler, oturduktan sonra ~0,6 sn boyunca ölçer, ortancayı
// verir. 0: tamam, 1: BOOT ile atlandı / zaman aşımı, 2: kısa ya da titrek basış (tekrar).
static int touchCapture(int& rx, int& ry, int& spread) {
  while (tsTouched()) delay(5);
  delay(120);
  uint32_t t0 = millis();
  while (!tsTouched()) {
    if (digitalRead(PIN_BOOT) == LOW || millis() - t0 > 60000) {
      while (digitalRead(PIN_BOOT) == LOW) delay(10);
      return 1;
    }
    delay(5);
  }
  static int xs[64], ys[64];
  int n = 0;
  delay(60);  // parmak otursun
  t0 = millis();
  while (millis() - t0 < 600 && n < 64) {
    TouchRaw p = tsRead();
    if (!p.z) break;
    xs[n] = p.x;
    ys[n] = p.y;
    n++;
    delay(8);
  }
  while (tsTouched()) delay(5);
  if (n < 8) return 2;
  rx = calMedian(xs, n);
  ry = calMedian(ys, n);
  // yayılım: ortancaya mutlak uzaklıkların ortancası (iki eksenin büyüğü)
  for (int i = 0; i < n; i++) {
    xs[i] = abs(xs[i] - rx);
    ys[i] = abs(ys[i] - ry);
  }
  spread = std::max(calMedian(xs, n), calMedian(ys, n));
  return spread > 40 ? 2 : 0;
}
