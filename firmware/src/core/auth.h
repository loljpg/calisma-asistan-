#pragma once
// Güvenlik: PIN, yetki seviyesi ve ekran kilidi (hesaplar core/authcalc.h'de, ekranlar os/authui.h'de).
// PIN NVS'de (cs) yalnızca rastgele 16 baytlık tuz ve SHA-256(tuz || PIN) olarak durur; hiçbir
// günlüğe ya da ekrana düz yazılmaz. Geliştirici seviyesi PIN'le açılır, 10 dk sonra kendiliğinden
// kapanır. Not: USB ile flash'ı okuyabilen biri NVS'yi de okur; PIN buna karşı koruma değildir
// (docs/guvenlik.md).
#include "authcalc.h"
#include "cmd.h"

static uint32_t authDevUntil = 0;   // geliştirici seviyesinin bittiği an (0: kapalı)
static uint32_t authWaitUntil = 0;  // yanlış denemelerden sonra bekleme
static uint8_t authFails = 0;
static bool authPinSet = false, authLoaded = false;
static volatile bool lockActive = false;  // ekran kilitli (yalnızca PIN girişi)

static void authLoad() {
  if (authLoaded) return;
  Lock l;
  prefs.begin("cs", true);
  authPinSet = prefs.getBytesLength("pinh") == 32;
  authFails = prefs.getUChar("pinf", 0);
  prefs.end();
  authLoaded = true;
  if (authFails >= 5) authWaitUntil = millis() + authDelayMs(authFails);  // açılışta da bekletilir
}

static bool authHasPin() {
  authLoad();
  return authPinSet;
}

static uint8_t authLevel() {
  if (authDevUntil && (int32_t)(millis() - authDevUntil) < 0) return CL_DEV;
  authDevUntil = 0;
  return CL_USER;
}

static uint32_t authDevLeftMin() { return authLevel() == CL_DEV ? (authDevUntil - millis() + 59999) / 60000 : 0; }

static void authDevOn() {
  authDevUntil = millis() + 600000UL;
  sysLog("güvenlik", "geliştirici seviyesi açıldı (10 dk)");
}
static void authDevOff() {
  if (authDevUntil) sysLog("güvenlik", "geliştirici seviyesi kapandı");
  authDevUntil = 0;
}

// Bekleme sürüyorsa kalan saniye, yoksa 0
static uint32_t authWaitSec() {
  int32_t d = (int32_t)(authWaitUntil - millis());
  return d > 0 ? (d + 999) / 1000 : 0;
}

// 1 doğru, 0 yanlış, -1 bekleme sürüyor (denenmedi)
static int authCheck(const String& pin) {
  authLoad();
  if (!authPinSet) return 1;
  if (authWaitSec()) return -1;
  uint8_t salt[16], want[32], got[32];
  {
    Lock l;
    prefs.begin("cs", true);
    prefs.getBytes("pins", salt, 16);
    prefs.getBytes("pinh", want, 32);
    prefs.end();
  }
  authHash(salt, pin, got);
  bool ok = authSame(got, want, 32);
  authFails = ok ? 0 : (uint8_t)std::min(250, authFails + 1);
  authWaitUntil = millis() + authDelayMs(authFails);
  {
    Lock l;
    prefs.begin("cs", false);
    prefs.putUChar("pinf", authFails);
    prefs.end();
  }
  if (!ok) sysLog("güvenlik", "yanlış PIN (" + String(authFails) + ". deneme)");
  return ok ? 1 : 0;
}

static bool authSetPin(const String& pin) {
  if (!authPinValid(pin)) return false;
  uint8_t salt[16], h[32];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = esp_random();
    memcpy(salt + i, &r, 4);
  }
  authHash(salt, pin, h);
  Lock l;
  prefs.begin("cs", false);
  prefs.putBytes("pins", salt, 16);
  prefs.putBytes("pinh", h, 32);
  prefs.putUChar("pinf", 0);
  prefs.end();
  authPinSet = true;
  authFails = 0;
  authWaitUntil = 0;
  sysLog("güvenlik", "PIN belirlendi");
  return true;
}

static void authClearPin() {
  {
    Lock l;
    prefs.begin("cs", false);
    prefs.remove("pins");
    prefs.remove("pinh");
    prefs.remove("pinf");
    prefs.end();
  }
  authPinSet = false;
  authFails = 0;
  authDevUntil = 0;
  lockActive = false;
  sysLog("güvenlik", "PIN kaldırıldı");
}
