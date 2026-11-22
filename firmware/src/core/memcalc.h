#pragma once
// DAXEN VANE · Bellek bütçesi kararı (saf hesap; bilgisayarda sınanır). Kullanımı: os/membudget.h,
// kurallar ve sayılar: docs/bellek.md.
#include <stdint.h>

#define MEM_FLOOR (18u * 1024u)  // boş bellek hiçbir işle bunun altına inmez (Wi-Fi, Bluetooth, ekran payı)
#define MEM_MARGIN (2u * 1024u)  // ölçüm ile ayırma arasındaki oynamalar için
#define MEM_HEAVY_MAX 3          // aynı anda en çok bu kadar ağır iş

// Ağır işler: kendi yığını ya da büyük tamponu olanlar (her biri tektir)
enum MemJob : uint8_t { MJ_LUA, MJ_WEB, MJ_NETTOOL, MJ_CHESS, MJ_BLESCAN, MJ_BENCH, MJ_N };
enum MemVerdict : uint8_t { MV_OK, MV_BUSY, MV_LOW, MV_FRAG };

// freeB: şu an boş bellek, maxBlk: en büyük boş blok, need: işin toplam gereksinimi,
// block: tek parça alınması gereken en büyük ayırma (genelde yığın), running: çalışan ağır işler (bit)
static MemVerdict memDecide(uint32_t freeB, uint32_t maxBlk, uint32_t need, uint32_t block, uint8_t running, uint8_t self) {
  uint8_t others = running & (uint8_t) ~(1u << self);
  int n = 0;
  for (; others; others &= others - 1) n++;
  if (n >= MEM_HEAVY_MAX) return MV_BUSY;
  if (freeB < need + MEM_FLOOR + MEM_MARGIN) return MV_LOW;
  if (maxBlk < block + 512) return MV_FRAG;
  return MV_OK;
}
