#pragma once
// PIN ve yetki hesapları (bilgisayarda test edilir); saklama ve ekranlar sys/auth.h'de.
// PIN 4–8 rakam. Yanlış denemede bekleme: ilk 4 yanlış serbest, 5.'den sonra 30 sn, her yanlışta
// iki katı, en çok 15 dk. Sayaç NVS'de durur; yeniden başlatmak beklemeyi sıfırlamaz.
#include <mbedtls/sha256.h>

static bool authPinValid(const String& p) {
  if (p.length() < 4 || p.length() > 8) return false;
  for (size_t i = 0; i < p.length(); i++)
    if (p[i] < '0' || p[i] > '9') return false;
  return true;
}

static uint32_t authDelayMs(uint8_t fails) {
  if (fails < 5) return 0;
  return std::min<uint32_t>(30000UL << std::min(5, fails - 5), 900000UL);
}

// SHA-256(tuz || PIN)
static void authHash(const uint8_t* salt, const String& pin, uint8_t* out) {
  uint8_t b[16 + 8];
  memcpy(b, salt, 16);
  memcpy(b + 16, pin.c_str(), pin.length());
  mbedtls_sha256_ret(b, 16 + pin.length(), out, 0);
  memset(b, 0, sizeof b);
}

// Sabit süreli karşılaştırma
static bool authSame(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}
