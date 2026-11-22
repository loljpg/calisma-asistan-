#pragma once
// YKS uygulaması: günün planı, ders kronometresi (Pomodoro, odak modu), soru sayacı, özet ve
// çalışma geçmişi. Telefondaki uygulamayla ortak çalışır (bulut ya da Bluetooth). Ana ekrandaki
// "şimdi / sıradaki" kartı da buradan çizilir.
//  - Soru sayacı: günde çözülen soru (derse göre), hedef, son 14 gün. NVS'de ("soru"); her değişiklik
//    birkaç saniye sonra "soru" olayı olarak buluta ve SD'deki kalıcı kayda gider.
//  - Çalışma geçmişi: son 12 haftanın kronometreyle çalışılan süresi (SD'deki aylık kalıcı kayıttan).
//  - Odak modu: telefondan gelen bildirim afişleri ve oyunlar kapanır; 3 saat sonra kendiliğinden biter.
#include "../os/shell.h"
#include "../core/soru.h"

static uint8_t yksTab = 0;  // 0 Plan, 1 Sayaç, 2 Soru, 3 Özet
static int yksFirst = 0;    // plan listesinde ilk görünen satır
static std::vector<Item> yksItems;
static const int YKS_ROW_H = 42, YKS_ROWS = 5, YKS_NTABS = 4;
static const char* const YKS_TABS[] = {"Plan", "Sayaç", "Soru", "Özet"};
static bool yksHist = false;  // Özet içinde çalışma geçmişi açık

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
  text("Pomodoro", 12, 178, F_SM, C_TEXT, C_BG, ML_DATUM);  // 25 dk çalış, 5 dk mola
  toggleSw(104, 168, cfg.pomodoro, C_BG);
  text("Odak modu", 172, 178, F_SM, focusOn ? C_ACC : C_TEXT, C_BG, ML_DATUM);
  toggleSw(272, 168, focusOn, C_BG);
  button(12, 198, 140, 36, swRunning ? "Duraklat" : (started ? "Devam et" : "Başlat"), B_PRIMARY, swRunning ? I_PAUSE : I_PLAY);
  button(160, 198, 72, 36, "Bitir", started ? B_NORMAL : B_DISABLED);
  button(240, 198, 68, 36, "Sıfırla", started ? B_NORMAL : B_DISABLED);
}

// --- Odak modu ---
static const uint32_t FOCUS_MS = 3UL * 3600UL * 1000UL;
static void focusSet(bool on) {
  focusOn = on;
  focusUntil = millis() + FOCUS_MS;
  sysLog("yks", on ? "odak modu açıldı" : "odak modu kapandı");
}

// --- Soru sayacı ---
static SoruLog sq;
static bool sqLoaded = false, sqPick = false;
static uint8_t sqDers = 1;            // seçili ders (SQ_DERS_AD)
static int16_t sqPend[SQ_DERS] = {0};  // buluta henüz gitmeyen
static uint32_t sqChangedAt = 0;
static const int SQ_STEPS[] = {-1, 1, 5, 10, 20};
static const uint16_t SQ_GOALS[] = {25, 50, 75, 100, 150, 200, 300, 400};

static uint32_t sqToday() {
  struct tm t;
  return localNow(t) ? (uint32_t)sqDayNum(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) : sq.day;
}
static void sqLoad() {
  if (sqLoaded) return;
  sqLoaded = true;
  {
    Lock l;
    prefs.begin("soru", true);
    if (prefs.getBytesLength("g") == sizeof sq) prefs.getBytes("g", &sq, sizeof sq);
    sqDers = std::min<uint8_t>(prefs.getUChar("ders", 1), SQ_DERS - 1);
    prefs.end();
  }
  if (sq.goal == 0 || sq.goal > 2000) sq.goal = 100;
  uint32_t d = sqToday();
  if (d) sqRoll(sq, d);
}
// NVS'ye yazar ve olayları sıraya koyar (son dokunuştan birkaç saniye sonra, kapanışta)
static void sqFlush() {
  if (!sqChangedAt) return;
  sqChangedAt = 0;
  {
    Lock l;
    prefs.begin("soru", false);
    prefs.putBytes("g", &sq, sizeof sq);
    prefs.putUChar("ders", sqDers);
    prefs.end();
  }
  for (int i = 0; i < SQ_DERS; i++)
    if (sqPend[i]) {
      enqueueSoru(SQ_DERS_AD[i], sqPend[i]);
      sqPend[i] = 0;
    }
}
static int sqDefaultDers() {  // kronometredeki ders listedeyse o
  int d = sqDersOf(swDers.c_str());
  return d >= 0 ? d : sqDers;
}

static void yksDrawSoruCount() {
  fillBg(12, 52, 296, 46);
  int n = sq.last[0];
  text(String(n), 12, 56, F_LG, C_TEXT, C_BG);
  int x = 18 + textW(String(n), F_LG);
  text("/ " + String(sq.goal) + " soru", x, 66, F_SM, C_TEXT2, C_BG);
  text("7 gün: " + String(sqSum(sq, 7)), 308, 66, F_XS, C_TEXT3, C_BG, TR_DATUM);
  progress(12, 92, 296, 3, sq.goal ? n * 100 / sq.goal : 0, n >= sq.goal ? C_ACC : C_TEXT2);
}

static void yksDrawSoruChart() {
  const int top = 156, h = 48, bw = 16, gap = 5, x0 = 12;
  fillBg(0, top - 12, SCR_W, SCR_H - top + 12);
  // bugün hangi derslerden
  String br;
  for (int k = 0; k < 3; k++) {
    int best = -1;
    for (int i = 0; i < SQ_DERS; i++)
      if (sq.ders[i] && (best < 0 || sq.ders[i] > sq.ders[best]) && br.indexOf(SQ_DERS_AD[i]) < 0) best = i;
    if (best < 0) break;
    br += (br.length() ? " · " : "") + String(SQ_DERS_AD[best]) + " " + String(sq.ders[best]);
  }
  text(fit(br.length() ? br : String("Bugün henüz soru yok"), F_XS, 296), 12, top - 8, F_XS, C_TEXT3, C_BG);
  int mx = sq.goal;
  for (int i = 0; i < SQ_DAYS; i++) mx = std::max<int>(mx, sq.last[i]);
  int gy = top + 8 + h - h * sq.goal / std::max(1, mx);  // hedef çizgisi
  for (int x = x0; x < x0 + SQ_DAYS * (bw + gap); x += 4) tft.drawPixel(x, gy, C_TEXT3);
  for (int i = 0; i < SQ_DAYS; i++) {
    int d = SQ_DAYS - 1 - i;  // soldan sağa: eskiden bugüne
    int v = sq.last[d], bh = v ? std::max(2, h * v / std::max(1, mx)) : 0;
    int x = x0 + i * (bw + gap), y = top + 8 + h - bh;
    if (bh) tft.fillRect(x, y, bw, bh, d == 0 ? C_ACC : (v >= sq.goal ? C_TEXT2 : C_LINE));
    tft.fillRect(x, top + 9 + h, bw, 1, C_LINE);
  }
  // gün adları: her ikinci gün
  for (int i = SQ_DAYS - 1; i >= 0; i -= 2) {
    int d = SQ_DAYS - 1 - i, wd = sqWeekday((int32_t)sq.day - d);
    static const char* const G[] = {"Pt", "Sa", "Ça", "Pe", "Cu", "Ct", "Pz"};
    text(d == 0 ? "bugün" : G[wd], x0 + i * (bw + gap) + bw / 2, top + 20 + h, F_XS, d == 0 ? C_ACC : C_TEXT3, C_BG, TC_DATUM);
  }
}

static void yksDrawSoruPick() {
  fillBg(0, 100, SCR_W, SCR_H - 100);
  text("Ders seç", 12, 108, F_XS, C_TEXT2, C_BG);
  for (int i = 0; i < SQ_DERS; i++) {
    int c = i % 3, r = i / 3, x = 12 + c * 100, y = 124 + r * 28;
    button(x, y, 96, 24, SQ_DERS_AD[i], i == sqDers ? B_PRIMARY : B_NORMAL);
  }
}

static void yksDrawSoru() {
  sqLoad();
  sqRoll(sq, sqToday());
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  text("Bugün çözülen", 12, 39, F_XS, C_TEXT2, C_BG);
  // ders seçici (sağ üst)
  panel(180, 34, 128, 18, C_SURF, C_LINE);
  text(fit(SQ_DERS_AD[sqDers], F_XS, 104), 188, 43, F_XS, C_TEXT, C_SURF, ML_DATUM);
  drawIcon(I_DOWN, 290, 37, 12, C_TEXT2, C_SURF);
  yksDrawSoruCount();
  for (int i = 0; i < 5; i++) {
    int v = SQ_STEPS[i];
    button(12 + i * 60, 104, 56, 34, (v > 0 ? "+" : "−") + String(abs(v)), i == 0 ? B_NORMAL : B_PRIMARY);
  }
  if (sqPick) yksDrawSoruPick();
  else yksDrawSoruChart();
}

static void yksSoruTap(int x, int y) {
  sqLoad();
  if (sqPick) {
    sqPick = false;
    if (y >= 124 && y < 124 + 4 * 28) {
      int i = ((y - 124) / 28) * 3 + constrain((x - 12) / 100, 0, 2);
      if (i >= 0 && i < SQ_DERS) sqDers = i, sqChangedAt = millis();
    }
    yksDrawSoru();
    return;
  }
  if (y < 56 && x >= 176) {
    sqPick = true;
    yksDrawSoru();
    return;
  }
  if (y >= 52 && y < 100 && x < 200) {  // hedef
    std::vector<String> g;
    for (uint16_t v : SQ_GOALS) g.push_back(String(v) + " soru");
    int cur = -1;
    for (int i = 0; i < 8; i++)
      if (SQ_GOALS[i] == sq.goal) cur = i;
    menu("Günlük hedef", {g.begin(), g.begin() + 6}, [](int c) {
      if (c < 0 || c >= 6) return;
      sq.goal = SQ_GOALS[c];
      sqChangedAt = millis();
    }, cur);
    return;
  }
  if (y >= 102 && y < 142) {
    int i = constrain((x - 10) / 60, 0, 4);
    int real = sqAdd(sq, sqToday(), sqDers, SQ_STEPS[i]);
    if (!real) return;
    sqPend[sqDers] += real;
    sqChangedAt = millis();
    yksDrawSoruCount();
    yksDrawSoruChart();
  }
}

// --- Çalışma geçmişi (Özet → Çalışma serisi) ---
static const int HIST_DAYS = 84;  // 12 hafta
// Son 84 günün kronometre dakikaları (hm[0] bugün); SD'deki aylık kalıcı kayıttan
static bool histLoad(uint16_t* hm, int32_t today) {
  memset(hm, 0, HIST_DAYS * sizeof hm[0]);
  if (!sdOk) return false;
  int32_t first = today - HIST_DAYS + 1;
  struct tm t;
  if (!localNow(t)) return false;
  int y = t.tm_year + 1900, m = t.tm_mon + 1;
  uint32_t sec[HIST_DAYS] = {0};
  for (int k = 0; k < 4; k++) {  // bu ay ve önceki üç ay
    char p[32];
    snprintf(p, sizeof p, "/calisma/kayit-%04d-%02d.txt", y, m);
    File f = SD.open(p);
    if (f) {
      char line[200];
      while (f.available()) {
        size_t n = f.readBytesUntil('\n', line, sizeof line - 1);
        line[n] = 0;
        int32_t day, s;
        if (histParseSession(line, day, s) && day >= first && day <= today) sec[today - day] += s;
      }
      f.close();
    }
    if (--m == 0) m = 12, y--;
  }
  for (int i = 0; i < HIST_DAYS; i++) hm[i] = std::min<uint32_t>(sec[i] / 60, 65000);
  return true;
}

static void yksDrawHist() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  struct tm t;
  if (!localNow(t)) {
    emptyState(HDR_H, SCR_H - HDR_H, I_CLOCK, "Saat ayarlanmadı", "Geçmiş için tarih gerekli.");
    return;
  }
  int32_t today = sqDayNum(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  std::unique_ptr<uint16_t[]> hm(new (std::nothrow) uint16_t[HIST_DAYS]);
  if (!hm || !histLoad(hm.get(), today)) {
    emptyState(HDR_H, SCR_H - HDR_H, I_SD, "SD kart gerekli", "Geçmiş kartın kalıcı kaydından okunur.");
    return;
  }
  text("Kronometreyle çalışılan, son 12 hafta", 12, 38, F_XS, C_TEXT2, C_BG);
  // ızgara: sütun hafta (sağda bu hafta), satır gün (pazartesi üstte)
  const int cs = 16, gp = 3, gx = 40, gy = 54;
  int wdToday = sqWeekday(today);
  const uint16_t lv[5] = {C_SURF, mix565(C_BG, C_ACC, 0.35f), mix565(C_BG, C_ACC, 0.6f), mix565(C_BG, C_ACC, 0.8f), C_ACC};
  for (int w = 0; w < 12; w++)
    for (int d = 0; d < 7; d++) {
      int ago = (11 - w) * 7 + (wdToday - d);  // bugünden kaç gün önce
      int x = gx + w * (cs + gp), y = gy + d * (cs + gp);
      if (ago < 0 || ago >= HIST_DAYS) continue;
      tft.fillRect(x, y, cs, cs, lv[histLevel(hm[ago])]);
      if (ago == 0) tft.drawRect(x - 1, y - 1, cs + 2, cs + 2, C_TEXT);
    }
  static const char* const G[] = {"Pt", "", "Ça", "", "Cu", "", "Pz"};
  for (int d = 0; d < 7; d++) text(G[d], 12, gy + d * (cs + gp) + cs / 2, F_XS, C_TEXT3, C_BG, ML_DATUM);
  int w7 = 0, w28 = 0, cur, best;
  for (int i = 0; i < 28; i++) (i < 7 ? w7 : w28) += hm[i];
  w28 += w7;
  histStreaks(hm.get(), HIST_DAYS, cur, best);
  int y = gy + 7 * (cs + gp) + 6;
  text("7 gün " + fmtMin(w7) + " · 4 hafta " + fmtMin(w28), 12, y, F_XS, C_TEXT, C_BG);
  text("Seri " + String(cur) + " gün · en uzun " + String(best), 12, y + 18, F_XS, C_TEXT2, C_BG);
  int lx = 222;  // ölçek
  text("az", lx - 4, y + 24, F_XS, C_TEXT3, C_BG, MR_DATUM);
  for (int i = 0; i < 5; i++) tft.fillRect(lx + i * 13, y + 19, 10, 10, lv[i]);
  text("çok", lx + 66, y + 24, F_XS, C_TEXT3, C_BG, ML_DATUM);
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
  sqLoad();
  sqRoll(sq, sqToday());
  Row rows[] = {
      {I_TARGET, "YKS'ye kalan", v.examDays > 0 ? String(v.examDays) + " gün" : (v.examDays == 0 ? String("Bugün") : String("—"))},
      {I_FLAME, "Çalışma serisi", v.streak > 0 ? String(v.streak) + " gün" : String("—")},
      {I_LIST, "Oturumlar", today ? String(done) + " / " + String(items.size()) + " tamam" : String("Plan yok")},
      {I_CHECK, "Soru", String(sq.last[0]) + " / " + String(sq.goal)},
      {I_REFRESH, "Eşitleme", q ? conn + " · " + String(q) + " bekliyor" : conn},
  };
  for (int i = 0; i < 5; i++) {
    int y = 102 + i * 27;
    hline(12, y, 296);
    drawIcon(rows[i].icon, 12, y + 5, 16, rows[i].icon == I_FLAME && v.streak > 0 ? C_ACC : C_TEXT2, C_BG);
    text(rows[i].k, 40, y + 14, F_SM, C_TEXT, C_BG, ML_DATUM);
    if (i == 1) {  // dokununca çalışma geçmişi
      text(fit(rows[i].val, F_SM, 130), 290, y + 14, F_SM, C_TEXT2, C_BG, MR_DATUM);
      drawIcon(I_RIGHT, 294, y + 7, 12, C_TEXT3, C_BG);
    } else {
      text(fit(rows[i].val, F_SM, 150), 308, y + 14, F_SM, C_TEXT2, C_BG, MR_DATUM);
    }
  }
}


static void yksDrawHeader() {
  header("YKS", true, false);
  segmented(102, 3, 210, 23, YKS_TABS, YKS_NTABS, yksTab);
}

static void yksDraw(bool full) {
  if (full) yksDrawHeader();
  if (yksTab == 0) yksDrawPlan();
  else if (yksTab == 1) yksDrawTimer();
  else if (yksTab == 2) yksDrawSoru();
  else if (yksHist) yksDrawHist();
  else yksDrawSummary();
}

static void yksSetTab(int t) {
  if (yksTab == 2 && t != 2) sqFlush();
  yksTab = t;
  yksHist = sqPick = false;
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
    if (x >= 102) yksSetTab(segmentedHit(x, 102, 210, YKS_NTABS));
    return;
  }
  if (yksTab == 2) {
    yksSoruTap(x, y);
    return;
  }
  if (yksTab == 3) {
    if (!yksHist && y >= 129 && y < 156) {  // Çalışma serisi satırı
      yksHist = true;
      yksDrawHist();
    } else if (!yksHist && y >= 183 && y < 210) {  // Soru satırı
      yksSetTab(2);
    }
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
      if (x < 160) {
        setPomodoro(!cfg.pomodoro);
        toggleSw(104, 168, cfg.pomodoro, C_BG);
      } else {
        focusSet(!focusOn);
        yksDrawTimer();
        toast(focusOn ? "Odak modu: bildirimler ve oyunlar 3 saat kapalı" : "Odak modu kapandı");
      }
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
  } else if (dir == SW_LEFT && yksTab < YKS_NTABS - 1) {
    yksSetTab(yksTab + 1);
  } else if (dir == SW_RIGHT && yksTab > 0) {
    yksSetTab(yksTab - 1);
  }
}

static void yksTick() {
  static uint32_t lastSec = 0;
  if (sqChangedAt && millis() - sqChangedAt > 4000) sqFlush();  // dokunuşlar bitince kaydet
  if (yksTab != 1) return;
  uint32_t s = swElapsedMs() / 1000;
  if (s != lastSec) {
    lastSec = s;
    yksDrawDigits();
  }
}

static void yksOpen() {
  if (swRunning || swElapsedMs() > 0) yksTab = 1;
  yksHist = sqPick = false;
}
static void yksClose() {
  sqFlush();
  yksItems.clear();
  yksItems.shrink_to_fit();
}
static bool yksBack() {  // geçmiş ya da ders seçimi açıksa önce o kapanır
  if (yksHist || sqPick) {
    yksHist = sqPick = false;
    yksDraw(false);
    return true;
  }
  return false;
}

static const App APP_YKS = {"YKS", "YKS", I_TARGET, yksOpen, yksDraw, yksTap, yksHold, yksSwipe, nullptr, yksTick, yksBack, yksClose};
