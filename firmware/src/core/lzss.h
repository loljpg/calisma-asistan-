#pragma once
// LZSS açıcı (bilgisayarda test edilir). Paketleyici: tools/pack_lua.py. Her 8 öğe için bir bayrak
// baytı (bit 1: düz bayt, 0: eşleşme); eşleşme 2 bayt, küçük uçlu: üst 12 bit uzaklık-1, alt 4 bit
// uzunluk-3. Çıkış tamponu aynı zamanda sözlüktür; ek bellek gerekmez. Bozuk veride 0 döner.
static size_t lzUnpack(const uint8_t* src, size_t n, uint8_t* out, size_t cap) {
  size_t i = 0, o = 0;
  while (i < n) {
    uint8_t f = src[i++];
    for (int b = 0; b < 8 && i < n; b++, f >>= 1) {
      if (f & 1) {
        if (o >= cap) return 0;
        out[o++] = src[i++];
        continue;
      }
      if (i + 1 >= n) return 0;
      uint16_t v = src[i] | src[i + 1] << 8;
      i += 2;
      size_t off = (v >> 4) + 1, len = (v & 15) + 3;
      if (off > o || o + len > cap) return 0;
      for (size_t k = 0; k < len; k++, o++) out[o] = out[o - off];
    }
  }
  return o;
}

// CRC32 (IEEE, yansıtılmış): c = crc32Upd(0, …) ile başlanır, parça parça sürdürülebilir
static uint32_t crc32Upd(uint32_t c, const uint8_t* p, size_t n) {
  c = ~c;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}
