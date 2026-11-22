#pragma once
// DAXEN VANE · Bellek bütçesi (docs/bellek.md). Ağır işler — Lua uygulaması, web sunucusu
// (Aktarım), ağ aracı, satranç motoru, Bluetooth taraması, kıyas testi — başlamadan önce memAsk'e sorar:
//  - gereksinim + 18 KB taban + 2 KB pay boş olmalı, en büyük blok işin yığınını almalı;
//  - aynı anda en çok MEM_HEAVY_MAX ağır iş çalışır.
// Reddedilen iş nedenini ve açık ağır işlerin adlarını söyler; kullanıcı birini kapatıp yeniden dener.
// Uygulamalardan sonra eklenir (çalışan işleri onların durumundan okur).
#include "../core/memcalc.h"

static const char* const MJ_NAME[MJ_N] = {"Lua uygulaması", "web sunucusu (Aktarım)", "ağ aracı", "satranç motoru",
                                          "Bluetooth taraması", "kıyas testi (Infinity)"};

// Şu an çalışan ağır işler (bit)
static uint8_t memRunning() {
  uint8_t m = 0;
#ifndef HOST
  if (kAlive(luaPid)) m |= 1 << MJ_LUA;
#else
  if (LS) m |= 1 << MJ_LUA;  // önizlemede süreç yok, durum doğrudan
#endif
  if (webRunning()) m |= 1 << MJ_WEB;
  if (ndBusy()) m |= 1 << MJ_NETTOOL;
  if (chAiBusy) m |= 1 << MJ_CHESS;
  if (bsBusy()) m |= 1 << MJ_BLESCAN;
  if (bnBusy()) m |= 1 << MJ_BENCH;
  return m;
}

static String memRunningNames(uint8_t m) {
  String s;
  for (int i = 0; i < MJ_N; i++)
    if (m & (1 << i)) s += (s.length() ? ", " : "") + String(MJ_NAME[i]);
  return s;
}

// İş başlayabilir mi? Hayırsa why'a kullanıcıya gösterilecek neden yazılır.
static bool memAsk(uint8_t job, uint32_t need, uint32_t block, String* why) {
  uint8_t run = memRunning();
  uint32_t fr = sysHeapFree(), mb = sysHeapMaxAlloc();
  MemVerdict v = memDecide(fr, mb, need, block, run, job);
  if (v == MV_OK) return true;
  String w;
  if (v == MV_BUSY)
    w = "Aynı anda en çok " + String(MEM_HEAVY_MAX) + " ağır iş çalışır; açık: " + memRunningNames(run & ~(1 << job));
  else if (v == MV_LOW)
    w = "Bellek yetersiz: " + String((need + MEM_FLOOR + MEM_MARGIN + 1023) / 1024) + " KB gerekiyor, " + String(fr / 1024) +
        " KB boş";
  else
    w = "Bellek parçalı: en büyük boş blok " + String(mb / 1024) + " KB, " + String((block + 512 + 1023) / 1024) + " KB gerekiyor";
  if (v != MV_BUSY && (run & ~(1 << job))) w += " (açık: " + memRunningNames(run & ~(1 << job)) + ")";
  static uint8_t lastJob = 0xFF, lastV = 0;  // aynı ret art arda günlüğü doldurmasın (ekranlar yeniden dener)
  static uint32_t lastAt = 0;
  if (job != lastJob || v != lastV || millis() - lastAt > 30000) {
    lastJob = job, lastV = v, lastAt = millis();
    sysLog("bellek", String(MJ_NAME[job]) + " başlamadı: " + w);
  }
  if (why) *why = w;
  return false;
}
