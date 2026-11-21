#pragma once
// Küçük sscanf (bilgisayarda C kütüphanesinin sscanf'iyle karşılaştırılarak test edilir;
// sys/libtrim.h bağlayıcıda sscanf ve siscanf'in yerine koyar). Desteklenen: boşluk, düz
// karakterler, %u %d %i %x (%x onaltılık), %[küme] (^ değil, a-z aralık), %n, genişlik (%2x,
// %10[…]) ve uzunluk ekleri hh (8 bit), h (16 bit), l (long). Ondalıklı sayı ve %s yok.
// Yazılımda kullananlar: Bluetooth denetleyicisinin sürüm okuması ("%u.%u.%u") ve saat dilimi
// çözümlemesi (tzset: "%10[-+0-9A-Za-z]%n", "%hu%n:%hu%n:%hu%n", "M%hu%n.%hu%n.%hu%n" …).
#include <stdarg.h>
#include <stdint.h>

static bool miniInSet(const char* p, const char* e, char c) {
  for (; p < e; p++) {
    if (p + 2 < e && p[1] == '-') {
      if (c >= p[0] && c <= p[2]) return true;
      p += 2;
    } else if (*p == c) {
      return true;
    }
  }
  return false;
}

static int miniVscan(const char* s, const char* f, va_list ap) {
  const char* s0 = s;
  int n = 0;
  while (*f) {
    if (*f == ' ' || *f == '\t' || *f == '\n') {
      while (*s == ' ' || *s == '\t' || *s == '\n') s++;
      f++;
      continue;
    }
    if (*f != '%' || f[1] == '%') {
      if (*f == '%') f++;
      if (*s != *f) break;
      s++, f++;
      continue;
    }
    f++;
    int w = 0, len = 0;  // len: 1 h, 2 hh, 3 l
    while (*f >= '0' && *f <= '9') w = w * 10 + *f++ - '0';
    if (*f == 'h') len = *++f == 'h' ? (f++, 2) : 1;
    else if (*f == 'l') len = 3, f++;
    char c = *f++;
    if (c == 'n') {  // şimdiye kadar okunan karakter (sayıma girmez)
      *va_arg(ap, int*) = (int)(s - s0);
      continue;
    }
    if (c == '[') {
      bool neg = *f == '^';
      if (neg) f++;
      const char* set = f;
      if (*f == ']') f++;
      while (*f && *f != ']') f++;
      const char* end = f;
      if (*f) f++;
      char* out = va_arg(ap, char*);
      int k = 0;
      while (*s && (!w || k < w) && miniInSet(set, end, *s) != neg) out[k++] = *s++;
      if (!k) break;
      out[k] = 0;
      n++;
      continue;
    }
    if (c != 'u' && c != 'd' && c != 'i' && c != 'x') break;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    const char* st = s;
    bool neg = false;
    if (c != 'x' && c != 'u' && (*s == '-' || *s == '+')) neg = *s++ == '-';
    unsigned long v = 0;
    int digits = 0, base = c == 'x' ? 16 : 10;
    while (!w || s - st < w) {
      int d = *s >= '0' && *s <= '9' ? *s - '0' : (base == 16 && (*s | 32) >= 'a' && (*s | 32) <= 'f') ? (*s | 32) - 'a' + 10 : -1;
      if (d < 0) break;
      v = v * base + d;
      s++, digits++;
    }
    if (!digits) break;
    if (neg) v = 0UL - v;
    void* p = va_arg(ap, void*);
    if (len == 2) *(uint8_t*)p = (uint8_t)v;
    else if (len == 1) *(uint16_t*)p = (uint16_t)v;
    else if (len == 3) *(unsigned long*)p = v;
    else *(unsigned int*)p = (unsigned int)v;
    n++;
  }
  return n;
}

static int miniScan(const char* s, const char* f, ...) {
  va_list ap;
  va_start(ap, f);
  int n = miniVscan(s, f, ap);
  va_end(ap);
  return n;
}
