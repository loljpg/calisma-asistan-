#pragma once
// DAXEN VANE sistem katmanı · BOOT tuşu, ışıklar, ekran parlaklığı, ışık sensörü.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// BOOT tuşu: kısa basış ana ekran, 2 sn Ayarlar, 6 sn Wi-Fi ayarı
// ============================================================================
static void handleBoot() {
  static bool down = false;
  static uint32_t downAt = 0;
  static int stage = 0;
  bool pressed = digitalRead(PIN_BOOT) == LOW;
  if (pressed && !down) {
    down = true;
    downAt = millis();
    stage = 0;
    lastInteract = millis();
  } else if (pressed && down) {
    uint32_t held = millis() - downAt;
    if (stage == 0 && held > 2000) {
      stage = 1;
      sysWake();
      shellBoot(1);
      toast("Bırakma: 6 sn'de Wi-Fi ayarı açılır");
    } else if (stage == 1 && held > 6000) {
      stage = 2;
      shellBoot(2);
    }
  } else if (!pressed && down) {
    down = false;
    if (stage == 0 && millis() - downAt > 40) {
      if (sysWake()) return;
      shellBoot(0);
    }
  }
}

// ============================================================================
// Işıklar ve ekran parlaklığı
// ============================================================================
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(1, 255 - r);  // LED'ler LOW'da yanar
  ledcWrite(2, 255 - g);
  ledcWrite(3, 255 - b);
}

static uint32_t ledOverrideUntil = 0;
static uint8_t ledOv[3];
static void sysLed(uint8_t r, uint8_t g, uint8_t b) {
  ledOv[0] = r;
  ledOv[1] = g;
  ledOv[2] = b;
  ledOverrideUntil = millis() + 400;
}

static void updateLeds() {
  uint32_t ms = millis();
  if (ledOverrideUntil && (int32_t)(ledOverrideUntil - ms) > 0) {
    setLed(ledOv[0], ledOv[1], ledOv[2]);
    return;
  }
  ledOverrideUntil = 0;
  bool blinkFast = (ms / 250) % 2, blinkSlow = (ms / 700) % 2;
  bool ok;
  {
    Lock l;
    ok = cloudOk;
  }
  if (otaProgress >= 0) setLed(0, 0, blinkFast ? 60 : 0);
  else if (pomoBreak) setLed(0, 0, blinkSlow ? 50 : 0);
  else if (swRunning) setLed(0, 35, 0);
  else if (upcomingSoon) setLed(0, 0, blinkSlow ? 40 : 0);
  else if (!ok && hasCreds() && ms > 90000 && !bleLinked) setLed(25, 0, 0);
  else setLed(0, 0, 0);
}

static bool isNightHour() {
  struct tm t;
  return localNow(t) && (t.tm_hour >= NIGHT_FROM || t.tm_hour < NIGHT_TO);
}

// Işık sensörü (GPIO34). Değer ne kadar yüksekse ortam o kadar karanlık.
static int sysLight() { return analogRead(PIN_LDR); }
static int sysLightPct() {
  int v = constrain(sysLight(), 0, 4095);
  return cfg.ldrInv ? v * 100 / 4095 : 100 - v * 100 / 4095;
}

static int blLevel = -1;
static void sysSetBacklight(int level) {
  level = constrain(level, 4, 255);
  if (level != blLevel) {
    blLevel = level;
    ledcWrite(0, level);
  }
}

// Gece kısma (22:00-07:00, dokununca 30 sn açılır) ve ışığa göre parlaklık
static void updateBacklight() {
  static uint32_t lastLdr = 0;
  static float ldrAvg = -1;
  uint32_t ms = millis();
  int level = cfg.bright;
  if (cfg.autoBright) {
    if (ms - lastLdr > 500 || ldrAvg < 0) {
      lastLdr = ms;
      int v = sysLightPct();
      ldrAvg = ldrAvg < 0 ? v : ldrAvg * 0.85f + v * 0.15f;
    }
    // aydınlıkta seçilen parlaklık, karanlıkta %20'ye kadar iner (sensör az ışıkta da hassas)
    float t = constrain(ldrAvg / 60.f, 0.f, 1.f);
    level = (int)(cfg.bright * (0.2f + 0.8f * t));
  }
  bool dim = cfg.nightDim && isNightHour() && (int32_t)(ms - wakeUntil) > 0;
  blDim = dim;
  if (dim) level = std::min(level, 40);
  sysSetBacklight(level);
}

// Gece kısıkken ilk dokunuş yalnızca ekranı açar.
static bool sysWake() {
  bool wasDim = blDim;
  wakeUntil = millis() + 30000;
  if (wasDim) {
    blDim = false;
    sysSetBacklight(cfg.bright);
  }
  return wasDim;
}
