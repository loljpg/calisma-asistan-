#pragma once
// Küçük sscanf (bilgisayarda test edilir; sys/libtrim.h bağlayıcıda sscanf'in yerine koyar).
// Desteklenen: boşluk, düz karakterler, %u %d %i %x (ondalık; %x onaltılık), genişlik (%2x) ve
// uzunluk ekleri hh (8 bit), h (16 bit), l (long). Ondalıklı sayı, dizgi ve %n yok: yazılımda
// sscanf'i yalnızca Bluetooth denetleyicisinin sürüm okuması ("v%u.%u.%u") kullanıyor.
#include <stdarg.h>
#include <stdint.h>

static int miniVscan(const char* s, const char* f, va_list ap) {
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
