#pragma once
// Görevler: çekirdeğin süreç tablosu (os/kernel.h). Servisler ve görevler, işlemci payları,
// yığın ve bellek. Bir sürece dokununca ayrıntısı açılır; görevler buradan sonlandırılabilir.
#include "../os/shell.h"
#include "../os/kernel.h"

static std::vector<int> tmPids;
static int tmTop = 0, tmPid = -1;  // tmPid >= 0: ayrıntı sayfası
static uint32_t tmAt = 0;
static const int TM_Y = HDR_H + 40, TM_RH = 24, TM_ROWS = 7;

static String tmPct(uint16_t p10) { return "%" + String(p10 / 10) + "," + String(p10 % 10); }

static String tmUs(uint32_t us) {
  if (us < 1000) {
    char b[12];
    snprintf(b, sizeof b, "0,%02u ms", (unsigned)((us + 5) / 10));
    return String(b);
  }
  if (us < 1000000) return String(us / 1000) + "," + String((us % 1000) / 100) + " ms";
  return String(us / 1000000) + "," + String((us % 1000000) / 100000) + " sn";
}

static String tmAge(uint32_t ms);
static String tmTotal(uint64_t us) {
  if (us < 60000000ULL) return tmUs((uint32_t)us);
  return tmAge((uint32_t)(us / 1000));
}

static String tmCount(uint32_t n) {  // 12.345
  String s = String(n);
  for (int i = (int)s.length() - 3; i > 0; i -= 3) s = s.substring(0, i) + "." + s.substring(i);
  return s;
}

static String tmAge(uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) return String(s) + " sn";
  if (s < 3600) return String(s / 60) + " dk " + String(s % 60) + " sn";
  return String(s / 3600) + " sa " + String((s / 60) % 60) + " dk";
}

static void tmCollect() {
  tmPids.clear();
  for (auto& p : kProcs)  // önce servisler (öncelik sırası), sonra görevler
    if (p.used && p.kind == PK_SERVICE) tmPids.push_back(p.pid);
  for (auto& p : kProcs)
    if (p.used && p.kind == PK_TASK) tmPids.push_back(p.pid);
  tmTop = constrain(tmTop, 0, std::max(0, (int)tmPids.size() - TM_ROWS));
}

static void tmDrawSummary() {
  fillBg(0, HDR_H, SCR_W, TM_Y - HDR_H);
  text("Çekirdek 1", 12, HDR_H + 6, F_XS, C_TEXT2, C_BG);
  progress(80, HDR_H + 11, 150, 4, kLoad10 / 10, C_ACC);
  text(tmPct(kLoad10), SCR_W - 12, HDR_H + 6, F_XS, C_TEXT, C_BG, TR_DATUM);
  String b = "Bellek " + fmtBytes(sysHeapFree()) + " boş · kesme " + String(K_HZ) + " Hz · " + String(tmPids.size()) + " süreç";
  text(fit(b, F_XS, SCR_W - 24), 12, HDR_H + 21, F_XS, C_TEXT3, C_BG);
  hline(0, TM_Y - 2, SCR_W);
}

static void tmDrawRow(int k) {
  int y = TM_Y + k * TM_RH;
  fillBg(0, y, SCR_W - 4, TM_RH);
  int idx = tmTop + k;
  if (idx >= (int)tmPids.size()) return;
  Proc* p = kFind(tmPids[idx]);
  if (!p) return;
  bool svc = p->kind == PK_SERVICE;
  drawIcon(svc ? I_REFRESH : I_CHIP, 10, y + 5, 16, svc ? C_TEXT3 : C_ACC, C_BG);
  text(fit(p->name, F_SM, 100), 34, y + 4, F_SM, C_TEXT, C_BG);
  String sub = svc ? String(p->period) + " ms" : String(kStateText(*p));
  text(fit(sub, F_XS, 112), 140, y + 6, F_XS, C_TEXT3, C_BG);
  if (p->measured) {
    text(tmPct(p->cpu10), SCR_W - 12, y + 4, F_SM, p->cpu10 >= 250 ? C_ACC : C_TEXT2, C_BG, TR_DATUM);
  } else {
    text("—", SCR_W - 12, y + 4, F_SM, C_TEXT3, C_BG, TR_DATUM);
  }
  hline(34, y + TM_RH - 1, SCR_W - 46);
}

static void tmDrawList() {
  for (int k = 0; k < TM_ROWS; k++) tmDrawRow(k);
  scrollBar(TM_Y, TM_ROWS * TM_RH, tmTop, TM_ROWS, tmPids.size());
}

static void tmDetailRow(int i, const String& k, const String& v) {
  int y = HDR_H + 8 + i * 22;
  fillBg(0, y, SCR_W, 22);
  text(k, 14, y + 3, F_SM, C_TEXT2, C_BG);
  text(fit(v, F_SM, 214), SCR_W - 14, y + 3, F_SM, C_TEXT, C_BG, TR_DATUM);
}

static void tmDrawDetail(bool full) {
  Proc* p = kFind(tmPid);
  if (!p) {
    tmPid = -1;
    toast("Süreç bitti");
    header("Görevler");
    tmCollect();
    tmDrawSummary();
    tmDrawList();
    return;
  }
  if (full) {
    header(p->name);
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  bool svc = p->kind == PK_SERVICE;
  tmDetailRow(0, "Tür", svc ? String("Servis · çekirdekte") : String(p->measured ? "Görev" : "Sistem görevi") + " · " + kStateText(*p));
  tmDetailRow(1, "Öncelik", svc ? String(p->prio + 1) + ". sırada" : "FreeRTOS " + String(p->prio) + " · çekirdek " + String(p->core));
  if (svc) tmDetailRow(2, "Periyot", String(p->period) + " ms (" + String(1000 / p->period) + " Hz)");
  else {
    uint32_t fr = kStackFree(*p);
    tmDetailRow(2, "Yığın", fmtBytes(p->stack) + (fr ? " · boş " + fmtBytes(fr) : String("")));
  }
  if (p->measured) {
    tmDetailRow(3, "Çalışma", tmCount(p->runs) + " kez");
    tmDetailRow(4, "Süre", "son " + tmUs(p->lastUs) + " · en uzun " + tmUs(p->maxUs));
    tmDetailRow(5, "İşlemci", tmPct(p->cpu10) + " · toplam " + tmTotal(p->totalUs));
  } else {
    tmDetailRow(3, "Çalışma", "çoğunlukla ağı bekler");
    tmDetailRow(4, "Süre", "ölçülmüyor");
    tmDetailRow(5, "İşlemci", "—");
  }
  tmDetailRow(6, "Açık", tmAge(millis() - p->startedMs));
  if (full) {
    if (p->killable && !p->done) button(14, SCR_H - 40, SCR_W - 28, 32, p->killReq ? "Sonlandırılıyor…" : "Sonlandır", p->killReq ? B_DISABLED : B_NORMAL, I_CLOSE);
    else text(svc ? "Servisler sistemin parçasıdır, sonlandırılamaz." : "Bu görev sistemin parçasıdır.", SCR_W / 2, SCR_H - 22, F_XS, C_TEXT3, C_BG, MC_DATUM);
  }
}

static void tmOpen() {
  tmPid = -1;
  tmTop = 0;
  tmCollect();
}

static void tmDraw(bool full) {
  if (tmPid >= 0) {
    tmDrawDetail(full);
    return;
  }
  if (full) header("Görevler");
  tmCollect();
  tmDrawSummary();
  tmDrawList();
}

static void tmTap(int x, int y) {
  if (tmPid >= 0) {
    Proc* p = kFind(tmPid);
    if (p && p->killable && !p->done && !p->killReq && y >= SCR_H - 44) {
      int pid = tmPid;
      dialog("Sonlandırılsın mı?", p->name + " kapanır; kaydedilmemiş verisi kaybolabilir.", {"Vazgeç", "Sonlandır"}, [pid](int c) {
        if (c == 1) {
          kKill(pid);
          toast("Sonlandırma istendi");
        }
      }, 1);
    }
    return;
  }
  if (y < TM_Y) return;
  int idx = tmTop + (y - TM_Y) / TM_RH;
  if (idx < 0 || idx >= (int)tmPids.size()) return;
  tmPid = tmPids[idx];
  tmDraw(true);
}

static void tmSwipe(int dir) {
  if (tmPid >= 0) return;
  int old = tmTop;
  if (dir == SW_UP) tmTop += 3;
  else if (dir == SW_DOWN) tmTop -= 3;
  tmCollect();
  if (tmTop != old) tmDrawList();
}

static void tmTick() {
  if (millis() - tmAt < 1000) return;  // kWindow ile aynı hızda
  tmAt = millis();
  if (tmPid >= 0) tmDrawDetail(false);
  else {
    tmCollect();
    tmDrawSummary();
    tmDrawList();
  }
}

static bool tmBack() {
  if (tmPid < 0) return false;
  tmPid = -1;
  tmDraw(true);
  return true;
}

static const App APP_TASKS = {"Görevler", "Görevler", I_PULSE, tmOpen, tmDraw, tmTap, nullptr, tmSwipe, nullptr, tmTick, tmBack, nullptr};
