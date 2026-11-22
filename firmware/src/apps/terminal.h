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
static bool tlFromSerial = false;  // çalışan komut seri porttan mı geldi
static void tlAppend(const String& s, bool echo);
static void tlDrawOut();
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
    if (t[i].help[0])  // yardımı boş olanlar gizli
      o.add(String("  ") + t[i].name + (t[i].sub ? String(" ") + t[i].sub : String("")) + (t[i].args[0] ? String(" ") + t[i].args : String("")) +
          " — " + t[i].help);
}

static void cClear(const std::vector<String>&, CmdOut& o) { o.clear = true; }

static void cHistory(const std::vector<String>&, CmdOut& o) {
  if (tlHist.items.empty()) o.add("Geçmiş boş");
  for (size_t i = 0; i < tlHist.items.size(); i++) o.add(String(i + 1) + "  " + tlHist.items[i]);
}

static void cUptime(const std::vector<String>&, CmdOut& o) { o.add(tlUptime()); }
static void cShot(const std::vector<String>& a, CmdOut& o) {  // screenshot [saniye]
  int s = a.size() > 1 ? constrain(a[1].toInt(), 0, 30) : 0;
  shotAt = millis() + 300 + s * 1000;
  o.add(s ? "ekran görüntüsü " + String(s) + " sn sonra alınır (/daxen/foto)" : String("ekran görüntüsü alınıyor (/daxen/foto)"));
}

static void cMemory(const std::vector<String>&, CmdOut& o) {
  uint32_t mn = sysHeapMin();
  o.kv("Boş", tlKB(sysHeapFree()));
  o.kv("En az (açılıştan beri)", tlKB(mn) + (mn < 18 * 1024 ? "  · 18 KB tabanının altında" : "  · taban tamam"));
  o.kv("En büyük blok", tlKB(sysHeapMaxAlloc()));
  o.kv("Bulut canlı akışı", netLiveOpen() ? "açık (şifreli bağlantı on binlerce bayt tutar)" : (netHold ? "duraklatıldı" : "kapalı"));
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

static void cWifiStatus(const std::vector<String>&, CmdOut& o) { ndConnInfo(o.lines); }

static void cWifiIp(const std::vector<String>&, CmdOut& o) {
  std::vector<String> all;
  ndConnInfo(all);
  for (auto& s : all)
    if (s.startsWith("Adres") || s.startsWith("Ağ geçidi") || s.startsWith("DNS") || s.startsWith("Wi-Fi")) o.add(s);
}

static void cWifiSaved(const std::vector<String>&, CmdOut& o) { ndSaved(o.lines); }

// Ağ aracını başlatır; çıktı geldikçe Terminal'e (ya da seri porta) yazılır. İlk kullanımda kartın
// ekranında onay istenir: araçlar yalnızca kullanma izni olunan ağlarda teşhis içindir.
static void tlStartJob(uint8_t job, const String& host, int arg, CmdOut& o) {
  bool ser = tlFromSerial;
  if (ndConsented) {
    ndToSerial = ser;
    if (!ndStart(job, host, arg)) o.add(ndError());
    return;
  }
  o.add("Ağ araçları için kartın ekranında onay ver");
  dialog("Ağ araçları", "Yalnızca kullanma iznin olan ağlarda teşhis için kullan. Devam edilsin mi?", {"Vazgeç", "Devam"},
         [job, host, arg, ser](int c) {
           if (c != 1) return;
           ndConsented = true;
           ndToSerial = ser;
           if (!ndStart(job, host, arg)) {
             if (ser) Serial.println(ndError());
             else {
               tlAppend(ndError(), false);
               if (tlVisible) tlDrawOut();
             }
           }
         });
}

static void cWifiScan(const std::vector<String>&, CmdOut& o) { tlStartJob(ND_SCAN, "", 0, o); }
static void cWifiPing(const std::vector<String>& a, CmdOut& o) {
  int n = a.size() > 1 ? a[1].toInt() : 4;
  tlStartJob(ND_PING, a[0], constrain(n, 1, 10), o);
}
static void cWifiDns(const std::vector<String>& a, CmdOut& o) { tlStartJob(ND_DNS, a[0], 0, o); }
static void cWifiTcp(const std::vector<String>& a, CmdOut& o) {
  int port = a[1].toInt();
  if (port < 1 || port > 65535) {
    o.add("Port 1–65535 olmalı");
    return;
  }
  tlStartJob(ND_TCP, a[0], port, o);
}
static void cWifiTrace(const std::vector<String>& a, CmdOut& o) { tlStartJob(ND_TRACE, a[0], 0, o); }
static void cWifiTest(const std::vector<String>&, CmdOut& o) { tlStartJob(ND_TEST, "", 0, o); }
static void cStop(const std::vector<String>&, CmdOut& o) {
  if (!ndBusy() && !bsBusy()) {
    o.add("Süren ağ ya da Bluetooth işi yok");
    return;
  }
  if (ndBusy()) ndCancel();
  if (bsBusy()) bsStop();
  o.add("Durduruluyor…");
}

static void cBtStatus(const std::vector<String>&, CmdOut& o) { bsInfo(o.lines); }

static bool tlBtSerial = false;  // Bluetooth taraması seri porttan mı başlatıldı
static void tlBtList(std::vector<String>& o) {
  int n = bsCount();
  uint32_t pk;
  uint16_t dr;
  bsStats(pk, dr);
  if (!n) {
    o.push_back(bsBusy() ? "Henüz cihaz yok" : "Liste boş (önce: bluetooth scan)");
    return;
  }
  blePush(o, "%d cihaz, %u paket (en güçlü sinyal önce):", n, (unsigned)pk);
  BleDev d[4];
  for (int i = 0; i < n; i += 4) {
    int got = bsGet(i, d, 4);
    for (int k = 0; k < got; k++) o.push_back("  " + bleRow(i + k + 1, d[k]));
  }
  if (dr) blePush(o, "  Liste dolu: %u zayıf cihaz alınmadı", (unsigned)dr);
  o.push_back("Ayrıntı: bluetooth info <no>");
}

static void cBtScan(const std::vector<String>& a, CmdOut& o) {
  int s = a.size() ? a[0].toInt() : 10;
  if (s < 3 || s > 30) {
    o.add("Süre 3–30 sn olmalı");
    return;
  }
  if (!bsStart(s * 1000UL, true)) {
    o.add(bsError());
    return;
  }
  tlBtSerial = tlFromSerial;
  blePush(o.lines, "Taranıyor (%d sn). %s", s, BLE_ONLY_ADS);
}

static void cBtDevices(const std::vector<String>&, CmdOut& o) { tlBtList(o.lines); }

static void cBtInfo(const std::vector<String>& a, CmdOut& o) {
  BleDev d;
  int no = a[0].toInt();
  if (no < 1 || bsGet(no - 1, &d, 1) != 1) {
    o.add("Listede böyle bir cihaz yok (liste: bluetooth devices)");
    return;
  }
  bleDetail(d, millis(), o.lines);
}

// --- Güvenlik ---
static void cDev(const std::vector<String>& a, CmdOut& o) {
  if (a.size() && a[0] == "off") {
    authDevOff();
    o.add("Geliştirici seviyesi kapandı");
    return;
  }
  if (authLevel() == CL_DEV) {
    blePush(o.lines, "Geliştirici seviyesi açık (%u dk kaldı). Kapatmak: dev off", (unsigned)authDevLeftMin());
    return;
  }
  if (!authHasPin()) {
    o.add("Önce Ayarlar → Güvenlik'te PIN belirle");
    return;
  }
  if (authWaitSec()) {
    blePush(o.lines, "Çok fazla yanlış PIN: %u sn bekle", (unsigned)authWaitSec());
    return;
  }
  o.add("PIN'i kartın ekranında gir (seri porta yazılmaz)");
  authAsk("Geliştirici seviyesi için PIN", []() {
    authDevOn();
    toast("Geliştirici seviyesi 10 dk açık");
    gizliDevFirst();
  });
}

static void cLogsClear(const std::vector<String>&, CmdOut& o) {
  o.add("Onay penceresi ekranda");
  dialog("Günlük silinsin mi?", "Bellekteki ve SD karttaki sistem günlüğü silinir.", {"Vazgeç", "Sil"}, [](int c) {
    if (c != 1) return;
    {
      Lock l;
      logRing.head = logRing.used = 0;
    }
    if (sdOk) {
      SD.remove("/daxen/sys/gunluk.txt");
      SD.remove("/daxen/sys/gunluk.1.txt");
    }
    sysLog("güvenlik", "sistem günlüğü silindi");
    toast("Günlük silindi");
  }, 1);
}

static void cBackup(const std::vector<String>&, CmdOut& o) {
  String m;
  backupSave(false, m);
  o.add(m + " (şifresiz; şifreli yedek: Ayarlar → Güvenlik)");
}

static void cRestore(const std::vector<String>&, CmdOut& o) {
  o.add("Onay penceresi ekranda");
  dialog("Yedekten geri yüklensin mi?", "Ekran, hava, Wi-Fi ve bulut ayarları yedektekiyle değişir.", {"Vazgeç", "Geri yükle"}, [](int c) {
    if (c != 1) return;
    String m;
    backupLoad(m);
    toast(m);
  }, 1);
}

// --- Gizli özellikler (yardımda görünmez; docs/gizli.md) ---
static void cGhost(const std::vector<String>&, CmdOut& o) {
  o.add("...");
  gizliAc("hayalet.lua");
}
static void cRadar(const std::vector<String>&, CmdOut&) { gizliAc("radar.lua"); }
static void cAwaken(const std::vector<String>&, CmdOut&) { gizliAc("uyanis.lua"); }

// --- Lua uygulamaları ---
static int tlLuaFind(const String& w) {
  String want = trLower(w);
  for (int i = 0; i < luaAppCount(); i++) {
    String stem = baseName(luaApps[i].path);
    stem = stem.substring(0, stem.lastIndexOf('.'));
    if (trLower(luaApps[i].name) == want || trLower(stem) == want) return i;
  }
  return -1;
}

// Günlüğün son n satırı (dosyanın son 2 KB'ı okunur)
static void tlLogTail(const String& path, int n, std::vector<String>& out) {
  File f = SD.open(path);
  String t;
  size_t from = 0;
  if (f) {
    size_t sz = f.size();
    from = sz > 2048 ? sz - 2048 : 0;
    f.seek(from);
    t = f.readString();
    f.close();
  }
  if (from) t = t.substring(t.indexOf('\n') + 1);
  std::vector<String> l;
  int a = 0;
  for (int c = t.indexOf('\n'); c >= 0; a = c + 1, c = t.indexOf('\n', a)) l.push_back(t.substring(a, c));
  if (l.empty()) out.push_back("  (günlük boş)");
  for (size_t k = l.size() > (size_t)n ? l.size() - n : 0; k < l.size(); k++) out.push_back("  " + l[k]);
}

static int tlLuaArg(const std::vector<String>& a, CmdOut& o) {
  luaScanned = false;
  int i = tlLuaFind(a[0]);
  if (i < 0) o.add("Böyle bir Lua uygulaması yok (liste: lua list)");
  return i;
}

static void cLuaList(const std::vector<String>&, CmdOut& o) {
  luaScanned = false;
  int n = luaAppCount();
  if (!n) o.add(sdOk ? "Lua uygulaması yok" : "SD kart takılı değil");
  for (int i = 0; i < n; i++) {
    const LuaAppInfo& x = luaApps[i];
    blePush(o.lines, "%s%s%s · %s · %s", x.name.c_str(), x.ver.length() ? " " : "", x.ver.c_str(), luaPermText(x.perm).c_str(),
            luaApproval(x.path) == luaWant(x) ? "onaylı" : "onay bekliyor");
  }
}

static void cLuaInfo(const std::vector<String>& a, CmdOut& o) {
  int i = tlLuaArg(a, o);
  if (i < 0) return;
  const LuaAppInfo& x = luaApps[i];
  uint64_t have = luaApproval(x.path), want = luaWant(x);
  o.kv("Ad", x.name);
  o.kv("Dosya", x.path);
  o.kv("Sürüm", x.ver.length() ? x.ver : String("yazılmamış"));
  o.kv("İzinler", luaPermText(x.perm) + (x.manifest ? "" : " (izin satırı yok, eski uygulama)"));
  o.kv("Onay", have == want ? "verildi" : have ? "dosya değişmiş, açılışta yeniden sorulur" : "ilk açılışta sorulur");
  o.kv("Arka plan", x.bg ? "evet" : "hayır");
  o.kv("Durum", kAlive(luaPid) && luaRunPath == x.path ? "çalışıyor" : "kapalı");
  o.add("Günlük, son satırlar:");
  tlLogTail(luaDataDirOf(x.path) + "/gunluk.txt", 8, o.lines);
}

static void cLuaLog(const std::vector<String>& a, CmdOut& o) {
  int i = tlLuaArg(a, o);
  if (i >= 0) tlLogTail(luaDataDirOf(luaApps[i].path) + "/gunluk.txt", a.size() > 1 ? constrain(a[1].toInt(), 1, 60) : 20, o.lines);
}

static void cLuaRemove(const std::vector<String>& a, CmdOut& o) {
  int i = tlLuaArg(a, o);
  if (i < 0) return;
  o.add("Onay penceresi ekranda");
  luaRemoveAsk(i);
}

static void cLuaStop(const std::vector<String>&, CmdOut& o) {
  if (!kAlive(luaPid)) {
    o.add("Çalışan Lua uygulaması yok");
    return;
  }
  String n = luaRunName;
  o.add(luaStopProc() ? "Durduruldu: " + n : String("Durdurulamadı"));
}

static const Cmd TL_CMDS[] = {
    {"help", nullptr, CL_USER, 0, 0, cHelp, "", "komutları listeler"},
    {"clear", nullptr, CL_USER, 0, 0, cClear, "", "ekranı temizler"},
    {"history", nullptr, CL_USER, 0, 0, cHistory, "", "son komutlar"},
    {"sysinfo", nullptr, CL_USER, 0, 0, cSysinfo, "", "sistem özeti"},
    {"version", nullptr, CL_USER, 0, 0, cVersion, "", "yazılım ve donanım sürümü"},
    {"uptime", nullptr, CL_USER, 0, 0, cUptime, "", "açılıştan beri geçen süre"},
    {"screenshot", nullptr, CL_USER, 0, 1, cShot, "[sn]", "ekran görüntüsü (/daxen/foto, BMP)"},
    {"memory", nullptr, CL_USER, 0, 0, cMemory, "", "bellek durumu"},
    {"tasks", nullptr, CL_USER, 0, 0, cTasks, "", "süreçler ve servisler"},
    {"apps", nullptr, CL_USER, 0, 0, cApps, "", "uygulamalar"},
    {"app", "launch", CL_USER, 1, 1, cAppLaunch, "<ad>", "uygulamayı açar"},
    {"storage", nullptr, CL_USER, 0, 0, cStorage, "", "SD kart ve yazılım alanı"},
    {"logs", "clear", CL_DEV, 0, 0, cLogsClear, "", "sistem günlüğünü siler (onaylı)"},
    {"logs", nullptr, CL_USER, 0, 1, cLogs, "[satır]", "sistem günlüğü"},
    {"wifi", "status", CL_USER, 0, 0, cWifiStatus, "", "bağlı ağın ayrıntıları"},
    {"wifi", "ip", CL_USER, 0, 0, cWifiIp, "", "adres, ağ geçidi, DNS"},
    {"wifi", "saved", CL_USER, 0, 0, cWifiSaved, "", "kayıtlı ağlar"},
    {"wifi", "scan", CL_USER, 0, 0, cWifiScan, "", "çevredeki ağlar (BSSID, kanal, güvenlik)"},
    {"wifi", "ping", CL_USER, 1, 2, cWifiPing, "<adres> [adet]", "ICMP yankı, gecikme ve kayıp"},
    {"wifi", "dns", CL_USER, 1, 1, cWifiDns, "<ad>", "DNS sorgusu ve süresi"},
    {"wifi", "tcp", CL_USER, 2, 2, cWifiTcp, "<adres> <port>", "TCP bağlantı testi"},
    {"wifi", "traceroute", CL_USER, 1, 1, cWifiTrace, "<adres>", "yol üzerindeki yönlendiriciler"},
    {"wifi", "test", CL_USER, 0, 0, cWifiTest, "", "ağ geçidi, DNS ve internet testi"},
    {"stop", nullptr, CL_USER, 0, 0, cStop, "", "süren ağ ya da Bluetooth işini durdurur"},
    {"bluetooth", "status", CL_USER, 0, 0, cBtStatus, "", "telefon, kartın adresi, tarama"},
    {"bluetooth", "scan", CL_USER, 0, 1, cBtScan, "[sn]", "yakındaki BLE cihazları"},
    {"bluetooth", "devices", CL_USER, 0, 0, cBtDevices, "", "son taramanın listesi"},
    {"bluetooth", "info", CL_USER, 1, 1, cBtInfo, "<no>", "cihazın ayrıntıları"},
    {"dev", nullptr, CL_USER, 0, 1, cDev, "[off]", "geliştirici seviyesi (PIN, 10 dk)"},
    {"ghost", nullptr, CL_USER, 0, 0, cGhost, "", ""},
    {"radar", nullptr, CL_USER, 0, 0, cRadar, "", ""},
    {"awaken", nullptr, CL_DEV, 0, 0, cAwaken, "", ""},
    {"backup", nullptr, CL_USER, 0, 0, cBackup, "", "ayarları SD'ye yedekler (şifresiz)"},
    {"restore", nullptr, CL_DEV, 0, 0, cRestore, "", "yedekten geri yükler (onaylı)"},
    {"lua", "list", CL_USER, 0, 0, cLuaList, "", "Lua uygulamaları, sürüm, izinler"},
    {"lua", "info", CL_USER, 1, 1, cLuaInfo, "<ad>", "izinler, onay, günlük"},
    {"lua", "log", CL_USER, 1, 2, cLuaLog, "<ad> [satır]", "uygulamanın günlüğü"},
    {"lua", "remove", CL_DEV, 1, 1, cLuaRemove, "<ad>", "uygulamayı kaldırır (onaylı)"},
    {"lua", "stop", CL_DEV, 0, 0, cLuaStop, "", "çalışan Lua uygulamasını durdurur"},
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
  cmdRun(t, n, line, authLevel(), o);
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
  if (!ndToSerial) {  // başka ekrandan kalmış ağ çıktısı gösterilmez
    std::vector<String> drop;
    ndDrain(drop);
  }
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

// Ağ aracının çıktısı geldikçe ekrana eklenir
static void tlTick() {
  static uint32_t at = 0;
  if (millis() - at < 150) return;
  at = millis();
  std::vector<String> l;
  if (!ndToSerial) ndDrain(l);
  if (!tlBtSerial && bsTakeDone()) tlBtList(l);
  if (l.empty()) return;
  for (auto& s : l) tlAppend(s, false);
  tlTop = 0;
  tlDrawOut();
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
  if (!ndToSerial) ndCancel();  // ekrandan başlatılan ağ işi ekranla biter
  if (!tlBtSerial) bsRelease(true);
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
        tlFromSerial = true;
        CmdOut o = termExec(buf);
        tlFromSerial = false;
        for (auto& s : o.lines) Serial.println(s);
      }
      buf = "";
    } else if (buf.length() < CMD_LINE_MAX + 4) {
      buf += c;
    }
  }
  std::vector<String> l;
  if (ndToSerial) ndDrain(l);
  if (tlBtSerial && bsTakeDone()) tlBtList(l);
  for (auto& s : l) Serial.println(s);
#endif
}

static const App APP_TERMINAL = {"Terminal", "Terminal", I_TERMINAL, tlOpen, tlDraw, tlTap, nullptr, tlSwipe, nullptr, tlTick, nullptr, tlClose};
