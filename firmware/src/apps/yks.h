#pragma once
// YKS uygulaması: günün planı, ders kronometresi (Pomodoro), özet. Telefondaki uygulamayla ortak
// çalışır (bulut ya da Bluetooth). Ana ekrandaki "şimdi / sıradaki" kartı da buradan çizilir.
#include "../os/shell.h"

static uint8_t yksTab = 0;  // 0 Plan, 1 Sayaç, 2 Özet
static int yksFirst = 0;    // plan listesinde ilk görünen satır
static std::vector<Item> yksItems;
static const int YKS_ROW_H = 42, YKS_ROWS = 5;
static const char* const YKS_TABS[] = {"Plan", "Sayaç", "Özet"};

// Süre: rakamlar F_LG, birimler F_SM (ör. 2 sa 5 dk)
static int drawDuration(int x, int y, int min, uint16_t bg = C_BG) {
  int h = std::max(0, min) / 60, m = std::max(0, min) % 60;
  auto part = [&](int v, const char* unit) {
    String n = String(v);
    text(n, x, y, F_LG, C_TEXT, bg);
    x += textW(n, F_LG) + 3;
    text(unit, x, y + 10, F_SM, C_TEXT2, bg);
    x += textW(unit, F_SM) + 8;
  };
  if (h) part(h, "sa");
  if (m || !h) part(m, "dk");
  return x;
}

// --- Ana ekran kartı ---
static void yksNowTimerTick(int x, int y, int w) {
  String t = fmtClock(swElapsedMs() / 1000);
  int tw = textW("0:00:00", F_LG);
  tft.fillRect(x + w - 12 - tw, y + 14, tw, 28, C_SURF);
  text(t, x + w - 12, y + 14, F_LG, C_TEXT, C_SURF, TR_DATUM);
  if (swDk > 0) progress(x + 12, y + 44, w - 24, 2, (int)(swElapsedMs() / 600 / swDk), C_ACC);
}

static void yksNowCard(int x, int y, int w, int h, bool tickOnly = false) {
  if (tickOnly) {
    if (swRunning) yksNowTimerTick(x, y, w);
    return;
  }
  panel(x, y, w, h);
  int studied = 0;
  View v;
  std::vector<Item> items = displayItems(studied, v);
  if (swRunning || swElapsedMs() > 0) {
    tft.fillRect(x + 12, y + 10, 6, 6, swRunning ? C_ACC : C_TEXT3);
    text(swRunning ? "Kronometre" : "Duraklatıldı", x + 24, y + 7, F_XS, C_TEXT2, C_SURF);
    int maxW = w - 36 - textW("0:00:00", F_LG);
    text(fit(swDers.length() ? swDers : String("Serbest çalışma"), F_MD, maxW), x + 12, y + 21, F_MD, C_TEXT, C_SURF);
    yksNowTimerTick(x, y, w);
    return;
  }
  String label, line1;
  uint16_t dot = C_TEXT3;
  if (!hasCreds() && !bleHasToken) {
    label = "Kurulum";
    line1 = "Telefonla eşleştir: Ayarlar";
  } else if (!planIsToday(v)) {
    bool ok;
    String msg;
    {
      Lock l;
      ok = cloudOk;
      msg = netMsg;
    }
    if (!ok && millis() > 45000) {
      label = "Bağlantı";
      dot = C_ACC;
      line1 = msg;
    } else {
      label = "Plan";
      line1 = v.valid ? "Bugünkü plan henüz gelmedi" : "Bugün için plan yok";
    }
  } else {
    int now = minutesOfDay();
    const Item* pick = nullptr;
    bool current = false;
    for (auto& it : items) {
      if (it.durum == "tamam") continue;
      int s = toMin(it.saat);
      if (now >= 0 && s + it.dk <= now) continue;
      pick = &it;
      current = now >= s;
      break;
    }
    if (pick) {
      label = current ? "Şimdi" : "Sıradaki";
      dot = current ? C_ACC : C_TEXT3;
      line1 = pick->saat + "  " + pick->ders + (pick->konu.length() ? " · " + pick->konu : String(""));
    } else {
      label = "Bugün";
      line1 = items.empty() ? "Plan boş" : "Bugünkü oturumlar bitti";
    }
  }
  tft.fillRect(x + 12, y + 10, 6, 6, dot);
  text(label, x + 24, y + 7, F_XS, C_TEXT2, C_SURF);
  text(fmtMin(studied) + " / " + fmtMin(v.goalMin), x + w - 12, y + 7, F_XS, C_TEXT3, C_SURF, TR_DATUM);
  text(fit(line1, F_MD, w - 24), x + 12, y + 21, F_MD, C_TEXT, C_SURF);
  progress(x + 12, y + 44, w - 24, 2, v.goalMin > 0 ? studied * 100 / v.goalMin : 0, C_TEXT2);
}

// --- Plan ---
static void yksDrawRow(int i, int y) {
  const Item& it = yksItems[i];
  bool isDone = it.durum == "tamam";
  bool active = swItemId.length() && swItemId == it.id;
  int now = minutesOfDay();
  bool late = !isDone && now >= 0 && toMin(it.saat) + it.dk < now;
  uint16_t bg = active ? C_PRESS : C_BG;
  tft.fillRect(0, y, SCR_W, YKS_ROW_H - 1, bg);
  hline(12, y + YKS_ROW_H - 1, SCR_W - 24);
  if (active) tft.fillRect(0, y + 6, 3, YKS_ROW_H - 13, C_ACC);
  text(it.saat, 12, y + YKS_ROW_H / 2, F_MD, late || it.durum == "ertelendi" ? C_ACC : (isDone ? C_TEXT3 : C_TEXT), bg, ML_DATUM);
  tft.fillRect(66, y + 9, 3, YKS_ROW_H - 19, dersCol(it.ders));
  String ders = fit(it.ders, F_MD, 186);
  text(ders, 78, y + 4, F_MD, isDone ? C_TEXT3 : C_TEXT, bg);
  if (isDone) tft.drawFastHLine(78, y + 14, textW(ders, F_MD), C_TEXT3);
  String sub = it.durum == "ertelendi" ? String("Ertelendi") : (it.konu.length() ? it.konu + " · " : String("")) + String(it.dk) + " dk";
  text(fit(sub, F_XS, 186), 78, y + 25, F_XS, it.durum == "ertelendi" || active ? C_ACC : C_TEXT2, bg);
  checkBox(284, y + 11, 20, isDone, bg);
}

static void yksDrawPlan() {
  int studied;
  View v;
  yksItems = displayItems(studied, v);
  if (!planIsToday(v)) yksItems.clear();
  int n = yksItems.size();
  if (!n) {
    emptyState(HDR_H, SCR_H - HDR_H, I_LIST, !hasCreds() && !bleHasToken ? "Telefonla eşleştirilmedi" : "Bugün için plan yok",
               "Plan telefondaki uygulamadan gelir.");
    return;
  }
  yksFirst = constrain(yksFirst, 0, std::max(0, n - YKS_ROWS));
  for (int r = 0; r < YKS_ROWS; r++) {
    int y = HDR_H + r * YKS_ROW_H;
    if (yksFirst + r < n) yksDrawRow(yksFirst + r, y);
    else fillBg(0, y, SCR_W, YKS_ROW_H);
  }
  scrollBar(HDR_H, YKS_ROW_H * YKS_ROWS, yksFirst, YKS_ROWS, n);
}

// --- Sayaç ---
static void yksDrawDigits() {
  uint32_t sec = swElapsedMs() / 1000;
  bigText(fmtClock(sec), 28, 76, SPR_W, 50, swRunning ? C_TEXT : C_TEXT2, C_BG, TC_DATUM);
  fillBg(12, 132, 296, 22);
  if (swDk > 0) {
    int pct = (int)(sec * 100 / (uint32_t)(swDk * 60));
    progress(12, 134, 296, 2, pct, C_ACC);
    text("Planlanan " + String(swDk) + " dk", 12, 141, F_XS, C_TEXT2, C_BG);
    text("%" + String(pct), 308, 141, F_XS, C_TEXT2, C_BG, TR_DATUM);
  } else if (pomoBreak) {
    text("Mola: 5 dakika ara ver", 160, 141, F_XS, C_ACC, C_BG, TC_DATUM);
  }
}

static void yksDrawTimer() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  bool started = swElapsedMs() > 0;
  tft.fillRect(12, 42, 6, 6, swRunning ? C_ACC : C_TEXT3);
  text(swRunning ? "Çalışıyor" : (started ? "Duraklatıldı" : "Hazır"), 24, 39, F_XS, C_TEXT2, C_BG);
  String label = swDers.length() ? swDers : String("Serbest çalışma");
  if (swKonu.length()) label += " · " + swKonu;
  text(fit(label, F_MD, 296), 12, 52, F_MD, C_TEXT, C_BG);
  yksDrawDigits();
  hline(12, 160, 296);
  text("Pomodoro", 12, 178, F_SM, C_TEXT, C_BG, ML_DATUM);
  text("25 dk çalış, 5 dk mola", 92, 178, F_XS, C_TEXT3, C_BG, ML_DATUM);
  toggleSw(272, 168, cfg.pomodoro, C_BG);
  button(12, 198, 140, 36, swRunning ? "Duraklat" : (started ? "Devam et" : "Başlat"), B_PRIMARY, swRunning ? I_PAUSE : I_PLAY);
  button(160, 198, 72, 36, "Bitir", started ? B_NORMAL : B_DISABLED);
  button(240, 198, 68, 36, "Sıfırla", started ? B_NORMAL : B_DISABLED);
}

// --- Özet ---
static void yksDrawSummary() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  int studied = 0;
  View v;
  std::vector<Item> items = displayItems(studied, v);
  bool today = planIsToday(v);
  int done = 0;
  for (auto& it : items)
    if (it.durum == "tamam") done++;
  text("Bugün çalışılan", 12, 40, F_XS, C_TEXT2, C_BG);
  drawDuration(12, 56, studied);
  text("/ " + fmtMin(v.goalMin), 308, 64, F_SM, C_TEXT2, C_BG, TR_DATUM);
  progress(12, 92, 296, 3, v.goalMin > 0 ? studied * 100 / v.goalMin : 0, C_ACC);
  struct Row {
    uint8_t icon;
    String k, val;
  };
  String conn;
  {
    Lock l;
    conn = bleLinked ? String("Bluetooth (telefon)") : netMsg;
  }
  int q = queueCount();
  Row rows[] = {
      {I_TARGET, "YKS'ye kalan", v.examDays > 0 ? String(v.examDays) + " gün" : (v.examDays == 0 ? String("Bugün") : String("—"))},
      {I_FLAME, "Çalışma serisi", v.streak > 0 ? String(v.streak) + " gün" : String("—")},
      {I_LIST, "Oturumlar", today ? String(done) + " / " + String(items.size()) + " tamam" : String("Plan yok")},
      {I_REFRESH, "Eşitleme", q ? conn + " · " + String(q) + " bekliyor" : conn},
  };
  for (int i = 0; i < 4; i++) {
    int y = 104 + i * 34;
    hline(12, y, 296);
    drawIcon(rows[i].icon, 12, y + 8, 18, rows[i].icon == I_FLAME && v.streak > 0 ? C_ACC : C_TEXT2, C_BG);
    text(rows[i].k, 40, y + 18, F_SM, C_TEXT, C_BG, ML_DATUM);
    text(fit(rows[i].val, F_SM, 150), 308, y + 18, F_SM, C_TEXT2, C_BG, MR_DATUM);
  }
}

static void yksDrawHeader() {
  header("YKS", true, false);
  segmented(118, 3, 194, 23, YKS_TABS, 3, yksTab);
}

static void yksDraw(bool full) {
  if (full) yksDrawHeader();
  if (yksTab == 0) yksDrawPlan();
  else if (yksTab == 1) yksDrawTimer();
  else yksDrawSummary();
}

static void yksSetTab(int t) {
  yksTab = t;
  yksDraw(true);
}

static void yksOpenMenu(const Item& it) {
  std::vector<String> opts;
  std::vector<int> acts;
  if (it.durum == "tamam") {
    opts.push_back("İşareti kaldır");
    acts.push_back(2);
  } else {
    opts.push_back("Kronometreyle başlat");
    acts.push_back(0);
    if (it.durum != "ertelendi") {
      opts.push_back("Ertele");
      acts.push_back(1);
    }
    opts.push_back("Tamamlandı say");
    acts.push_back(2);
  }
  opts.push_back("Vazgeç");
  acts.push_back(-1);
  Item copy = it;
  menu(it.saat + " · " + it.ders, opts, [copy, acts](int i) {
    if (i < 0 || i >= (int)acts.size()) return;
    switch (acts[i]) {
      case 0: startItemTimer(copy); yksSetTab(1); break;
      case 1: postponeItem(copy); break;
      case 2: toggleItem(copy); break;
      default: break;
    }
  }, 0);
}

static void yksTap(int x, int y) {
  if (y < HDR_H) {
    if (x >= 118) yksSetTab(segmentedHit(x, 118, 194, 3));
    return;
  }
  if (yksTab == 0) {
    int r = (y - HDR_H) / YKS_ROW_H, i = yksFirst + r;
    if (r < 0 || r >= YKS_ROWS || i >= (int)yksItems.size()) return;
    Item it = yksItems[i];
    if (x >= 262) {
      toggleItem(it);
    } else {
      selectForTimer(it);
      yksSetTab(1);
    }
  } else if (yksTab == 1) {
    bool started = swElapsedMs() > 0;
    if (y >= 160 && y < 194) {
      setPomodoro(!cfg.pomodoro);
      toggleSw(272, 168, cfg.pomodoro, C_BG);
    } else if (y >= 194) {
      if (x < 156) timerToggleRun();
      else if (x < 236) {
        if (started) finishTimer();
      } else if (started) {
        dialog("Kronometre sıfırlansın mı?", "Geçen süre kaydedilmez.", {"Vazgeç", "Sıfırla"}, [](int c) {
          if (c == 1) timerReset();
        });
      }
    }
  }
}

static bool yksHold(int x, int y) {
  if (yksTab != 0 || y < HDR_H) return false;
  int r = (y - HDR_H) / YKS_ROW_H, i = yksFirst + r;
  if (r < 0 || r >= YKS_ROWS || i >= (int)yksItems.size()) return false;
  yksOpenMenu(yksItems[i]);
  return true;
}

static void yksSwipe(int dir) {
  if (yksTab == 0 && (dir == SW_UP || dir == SW_DOWN)) {
    int n = yksItems.size();
    int nf = constrain(yksFirst + (dir == SW_UP ? YKS_ROWS - 1 : -(YKS_ROWS - 1)), 0, std::max(0, n - YKS_ROWS));
    if (nf != yksFirst) {
      yksFirst = nf;
      yksDrawPlan();
    }
  } else if (dir == SW_LEFT && yksTab < 2) {
    yksSetTab(yksTab + 1);
  } else if (dir == SW_RIGHT && yksTab > 0) {
    yksSetTab(yksTab - 1);
  }
}

static void yksTick() {
  static uint32_t lastSec = 0;
  if (yksTab != 1) return;
  uint32_t s = swElapsedMs() / 1000;
  if (s != lastSec) {
    lastSec = s;
    yksDrawDigits();
  }
}

static void yksOpen() {
  if (swRunning || swElapsedMs() > 0) yksTab = 1;
}

static const App APP_YKS = {"YKS", "YKS", I_TARGET, yksOpen, yksDraw, yksTap, yksHold, yksSwipe, nullptr, yksTick, nullptr, nullptr};
