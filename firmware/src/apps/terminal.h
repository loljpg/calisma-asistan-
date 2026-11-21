#pragma once
// Terminal: DAXEN VANE'in kendi komut yorumlayıcısı (core/cmd.h). Bir Linux kabuğu değildir;
// yalnızca aşağıdaki tablodaki komutlar çalışır ve hepsi gerçek değer okur. Aynı komutlar USB seri
// porttan da (115200, satır sonu \n) kullanılabilir. Çıktı ve geçmiş yalnızca ekran açıkken
// bellekte tutulur (en fazla 120 satır).
#include "../os/shell.h"
#include "../os/kernel.h"
#include "../core/cmd.h"
#include "taskman.h"

static std::vector<String> tlOut;  // ekrandaki satırlar
static CmdHistory tlHist;
static int tlTop = 0;              // kaydırma (en alttan kaç satır yukarıda)
static int tlHistPos = -1;
static bool tlVisible = false;     // Terminal ekranda mı (seri porttan gelen komut ekrana çizilmez)
static const int TL_Y = HDR_H + 2, TL_RH = 13, TL_ROWS = 12, TL_CHIP_Y = 198, TL_IN_Y = 220;
static const char* const TL_CHIPS[] = {"help", "sysinfo", "memory", "tasks", "logs"};

static String tlKB(uint32_t b) { return String(b / 1024) + " KB"; }

static String tlUptime() {
  uint32_t s = millis() / 1000;
  return String(s / 3600) + " sa " + String((s / 60) % 60) + " dk " + String(s % 60) + " sn";
}

// --- Komutlar ---------------------------------------------------------------
static const Cmd* tlTable(size_t& n);

static void cHelp(const std::vector<String>&, CmdOut& o) {
  size_t n;
  const Cmd* t = tlTable(n);
  o.add("Komutlar (bu bir Linux kabuğu değil):");
  for (size_t i = 0; i < n; i++)
    o.add(String("  ") + t[i].name + (t[i].sub ? String(" ") + t[i].sub : String("")) + (t[i].args[0] ? String(" ") + t[i].args : String("")) +
          " — " + t[i].help);
}

static void cClear(const std::vector<String>&, CmdOut& o) { o.clear = true; }

static void cHistory(const std::vector<String>&, CmdOut& o) {
  if (tlHist.items.empty()) o.add("Geçmiş boş");
  for (size_t i = 0; i < tlHist.items.size(); i++) o.add(String(i + 1) + "  " + tlHist.items[i]);
}

static void cUptime(const std::vector<String>&, CmdOut& o) { o.add(tlUptime()); }

static void cMemory(const std::vector<String>&, CmdOut& o) {
  uint32_t mn = sysHeapMin();
  o.kv("Boş", tlKB(sysHeapFree()));
  o.kv("En az (açılıştan beri)", tlKB(mn) + (mn < 18 * 1024 ? "  · 18 KB tabanının altında" : "  · taban tamam"));
  o.kv("En büyük blok", tlKB(sysHeapMaxAlloc()));
  o.kv("Günlük halkası", String(logRing.used) + " / " + String(LOG_RING) + " B");
}

static void cVersion(const std::vector<String>&, CmdOut& o) {
  o.kv("Yazılım", "fw-" + String(FW_BUILD) + " · " FW_VARIANT);
  o.kv("Donanım", sysChipText());
  o.kv("Lua", LUA_RELEASE);
}

static void cSysinfo(const std::vector<String>& a, CmdOut& o) {
  cVersion(a, o);
  o.kv("Çalışma süresi", tlUptime());
  o.add(bootReason);  // "Son açılış: …"
  if (diagLastCrash.length()) o.kv("Önceki çökme", diagLastCrash);
  o.kv("Bellek", tlKB(sysHeapFree()) + " boş · en az " + tlKB(sysHeapMin()) + " · blok " + tlKB(sysHeapMaxAlloc()));
  o.kv("SD kart", sdOk ? "takılı" : "yok");
  o.kv("Wi-Fi", wifiConnected() ? wifiSsid() + " · " + wifiIp() + " · " + String(wifiRssi()) + " dBm" : String("bağlı değil"));
  o.kv("Bluetooth", bleLinked ? "telefon bağlı" : (bleHasToken ? "eşleşmiş, bağlı değil" : "eşleşme yok"));
  o.kv("Çekirdek 1 yükü", tmPct(kLoad10));
}

static void cTasks(const std::vector<String>&, CmdOut& o) {
  o.add("ad              tür     durum       işlemci  yığın boş");
  for (int pass = 0; pass < 2; pass++)
    for (auto& p : kProcs) {
      if (!p.used || (pass == 0) != (p.kind == PK_SERVICE)) continue;
      String name = p.name;
      while (name.length() < 15) name += ' ';
      String kind = p.kind == PK_SERVICE ? "servis " : "görev  ";
      String st = kStateText(p);
      while (st.length() < 11) st += ' ';
      String cpu = p.measured ? tmPct(p.cpu10) : String("—");
      while (cpu.length() < 8) cpu += ' ';
      uint32_t fr = p.kind == PK_SERVICE ? 0 : kStackFree(p);
      o.add(name + " " + kind + " " + st + " " + cpu + " " + (fr ? fmtBytes(fr) : String("—")));
    }
}

static void cApps(const std::vector<String>&, CmdOut& o) {
  o.add("Yerleşik:");
  String line = " ";
  for (int i = 0; i < appTableCount(); i++) {
    line += String(" ") + appTable()[i]->label;
    if (line.length() > 44) {
      o.add(line);
      line = " ";
    }
  }
  if (line.length() > 1) o.add(line);
  int n = luaAppCount();
  o.add("Lua (" + String(n) + "):");
  for (int i = 0; i < n; i++) o.add("  " + luaAppLabel(i));
}

static void cAppLaunch(const std::vector<String>& a, CmdOut& o) {
  String want = trLower(a[0]);
  for (int i = 0; i < appTableCount(); i++)
    if (trLower(appTable()[i]->label) == want || trLower(appTable()[i]->name) == want) {
      o.add("Açılıyor: " + String(appTable()[i]->label));
      appOpen(appTable()[i]);
      return;
    }
  for (int i = 0; i < luaAppCount(); i++)
    if (trLower(luaAppLabel(i)) == want) {
      o.add("Açılıyor: " + luaAppLabel(i));
      luaAppOpen(i);
      return;
    }
  o.add("Böyle bir uygulama yok (liste için: apps)");
}

static void cStorage(const std::vector<String>&, CmdOut& o) {
  if (sdOk) {
    uint64_t tot = SD.totalBytes(), used = SD.usedBytes();
    o.kv("SD kart", fmtBytes(used) + " dolu / " + fmtBytes(tot) + " · " + fmtBytes(tot - used) + " boş");
  } else {
    o.kv("SD kart", "takılı değil");
  }
  o.kv("Yazılım yuvası", fmtBytes(sysSketchSize()) + " / " + fmtBytes(sysSketchSize() + sysSketchFree()));
  o.kv("Günlük dosyası", sdOk && SD.exists("/daxen/sys/gunluk.txt") ? "/daxen/sys/gunluk.txt" : String("yok"));
}

static void cLogs(const std::vector<String>& a, CmdOut& o) {
  int n = a.size() ? a[0].toInt() : 20;
  if (n <= 0) n = 20;
  std::vector<String> t;
  {
    Lock l;
    t = logTail(logRing, n);
  }
  if (t.empty()) o.add("Günlük boş");
  for (auto& s : t) o.add(s);
}

static void cReboot(const std::vector<String>&, CmdOut& o) {
  o.add("Onay penceresi ekranda");
  dialog("Yeniden başlatılsın mı?", "Kaydedilmemiş veriler kaybolabilir.", {"Vazgeç", "Başlat"}, [](int c) {
    if (c == 1) sysRestart();
  }, 1);
}

static void cWifiStatus(const std::vector<String>&, CmdOut& o) {
  if (!wifiConnected()) {
    o.add("Wi-Fi'ye bağlı değil");
    return;
  }
  o.kv("Ağ", wifiSsid());
  o.kv("Adres", wifiIp());
  o.kv("Sinyal", String(wifiRssi()) + " dBm");
}

static void cBtStatus(const std::vector<String>&, CmdOut& o) {
  o.kv("Telefon", bleLinked ? "bağlı" : (bleHasToken ? "eşleşmiş, bağlı değil" : "eşleşme yok"));
}

static const Cmd TL_CMDS[] = {
    {"help", nullptr, CL_USER, 0, 0, cHelp, "", "komutları listeler"},
    {"clear", nullptr, CL_USER, 0, 0, cClear, "", "ekranı temizler"},
    {"history", nullptr, CL_USER, 0, 0, cHistory, "", "son komutlar"},
    {"sysinfo", nullptr, CL_USER, 0, 0, cSysinfo, "", "sistem özeti"},
    {"version", nullptr, CL_USER, 0, 0, cVersion, "", "yazılım ve donanım sürümü"},
    {"uptime", nullptr, CL_USER, 0, 0, cUptime, "", "açılıştan beri geçen süre"},
    {"memory", nullptr, CL_USER, 0, 0, cMemory, "", "bellek durumu"},
    {"tasks", nullptr, CL_USER, 0, 0, cTasks, "", "süreçler ve servisler"},
    {"apps", nullptr, CL_USER, 0, 0, cApps, "", "uygulamalar"},
    {"app", "launch", CL_USER, 1, 1, cAppLaunch, "<ad>", "uygulamayı açar"},
    {"storage", nullptr, CL_USER, 0, 0, cStorage, "", "SD kart ve yazılım alanı"},
    {"logs", nullptr, CL_USER, 0, 1, cLogs, "[satır]", "sistem günlüğü"},
    {"wifi", "status", CL_USER, 0, 0, cWifiStatus, "", "Wi-Fi bağlantısı"},
    {"bluetooth", "status", CL_USER, 0, 0, cBtStatus, "", "telefon bağlantısı"},
    {"reboot", nullptr, CL_USER, 0, 0, cReboot, "", "yeniden başlatır (onaylı)"},
};
static const Cmd* tlTable(size_t& n) {
  n = sizeof(TL_CMDS) / sizeof(TL_CMDS[0]);
  return TL_CMDS;
}

// Bir satırı çalıştırır ve çıktıyı döndürür (ekran ve seri port ortak)
static CmdOut termExec(const String& line) {
  CmdOut o;
  size_t n;
  const Cmd* t = tlTable(n);
  tlHist.push(line);
  cmdRun(t, n, line, CL_USER, o);
  return o;
}

// --- Ekran ------------------------------------------------------------------
static void tlAppend(const String& s, bool echo) {
  for (auto& part : wrapText(s.length() ? s : String(" "), F_XS, 300)) tlOut.push_back((echo ? "\x01" : "") + part);
  if (tlOut.size() > 120) tlOut.erase(tlOut.begin(), tlOut.begin() + (tlOut.size() - 120));
}

static void tlDrawOut() {
  fillBg(0, TL_Y, SCR_W, TL_CHIP_Y - TL_Y - 2);
  int total = tlOut.size();
  tlTop = constrain(tlTop, 0, std::max(0, total - TL_ROWS));
  int first = std::max(0, total - TL_ROWS - tlTop);
  for (int r = 0; r < TL_ROWS && first + r < total; r++) {
    const String& s = tlOut[first + r];
    bool echo = s.length() && s[0] == '\x01';
    text(echo ? s.substring(1) : s, 8, TL_Y + 2 + r * TL_RH, F_XS, echo ? C_ACC : C_TEXT2, C_BG);
  }
  if (total > TL_ROWS) scrollBar(TL_Y, TL_ROWS * TL_RH, first, TL_ROWS, total);
}

static void tlDrawBar() {
  fillBg(0, TL_CHIP_Y - 2, SCR_W, SCR_H - TL_CHIP_Y + 2);
  for (int i = 0; i < 5; i++) {
    int x = 4 + i * 63;
    panel(x, TL_CHIP_Y, 60, 19, C_SURF, C_LINE);
    text(TL_CHIPS[i], x + 30, TL_CHIP_Y + 10, F_XS, C_TEXT2, C_SURF, MC_DATUM);
  }
  panel(4, TL_IN_Y, 268, 18, C_SURF, C_ACC);
  text("> komut yaz", 12, TL_IN_Y + 9, F_XS, C_TEXT3, C_SURF, ML_DATUM);
  panel(276, TL_IN_Y, 40, 18, C_SURF, C_LINE);
  text("son", 296, TL_IN_Y + 9, F_XS, C_TEXT2, C_SURF, MC_DATUM);
}

static void tlRun(const String& line) {
  String l = line;
  l.trim();
  if (!l.length()) return;
  tlAppend("> " + l, true);
  CmdOut o = termExec(l);
  if (o.clear) tlOut.clear();
  for (auto& s : o.lines) tlAppend(s, false);
  tlTop = 0;
  if (tlVisible) tlDrawOut();
}

static void tlOpen() {
  tlVisible = true;
  tlOut.clear();
  tlTop = 0;
  tlHistPos = -1;
  tlAppend("DAXEN VANE terminali. Komutlar için: help", false);
}

static void tlDraw(bool full) {
  if (full) header("Terminal");
  tlDrawOut();
  if (full) tlDrawBar();
}

static void tlAsk(const String& initial) {
  kbOpen("Komut", initial, [](bool ok, const String& v) {
    shellRedraw(true);
    if (ok) tlRun(v);
  }, false, CMD_LINE_MAX, "help yazarak başla");
}

static void tlTap(int x, int y) {
  if (y >= TL_CHIP_Y && y < TL_CHIP_Y + 20) {
    int i = (x - 4) / 63;
    if (i >= 0 && i < 5) tlRun(TL_CHIPS[i]);
  } else if (y >= TL_IN_Y - 1) {
    if (x >= 274) {  // son komutlar arasında geriye doğru
      if (tlHist.items.empty()) {
        toast("Geçmiş boş");
        return;
      }
      tlHistPos = tlHistPos <= 0 ? (int)tlHist.items.size() - 1 : tlHistPos - 1;
      tlAsk(tlHist.items[tlHistPos]);
    } else {
      tlHistPos = -1;
      tlAsk("");
    }
  }
}

static void tlSwipe(int dir) {
  int old = tlTop;
  if (dir == SW_DOWN) tlTop += 6;  // parmak aşağı: eski satırlar
  else if (dir == SW_UP) tlTop -= 6;
  tlTop = std::max(0, tlTop);
  if (tlTop != old) tlDrawOut();
}

static void tlClose() {
  tlVisible = false;
  tlOut.clear();
  tlOut.shrink_to_fit();
}

// USB seri porttan gelen satırlar (saat servisinden çağrılır). Çıktı seri porta yazılır.
static void termSerialPoll() {
#ifndef HOST
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf.trim();
      if (buf.length()) {
        Serial.println("> " + buf);
        CmdOut o = termExec(buf);
        for (auto& s : o.lines) Serial.println(s);
      }
      buf = "";
    } else if (buf.length() < CMD_LINE_MAX + 4) {
      buf += c;
    }
  }
#endif
}

static const App APP_TERMINAL = {"Terminal", "Terminal", I_CODE, tlOpen, tlDraw, tlTap, nullptr, tlSwipe, nullptr, nullptr, nullptr, tlClose};
