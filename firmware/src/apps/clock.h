#pragma once
// Saat: Alarm, Sayaç (geri sayım), Kronometre, Dünya saatleri. Alarm ve sayaç arka planda çalışır;
// çalınca ekran tam parlaklıkta yanıp söner, LED yanar (cihazda hoparlör yok).
#include "../os/shell.h"

// ============================================================================
// Veri
// ============================================================================
struct Alarm {
  uint8_t h = 7, m = 0, days = 0x7F;  // days: bit0 Pzt … bit6 Paz; 0: bir kez
  bool on = true;
};
static Alarm alarms[6];
static int alarmN = 0;
static bool alarmsLoaded = false;

static void alarmsSave() {
  uint8_t b[24];
  for (int i = 0; i < alarmN; i++) {
    b[i * 4] = alarms[i].h;
    b[i * 4 + 1] = alarms[i].m;
    b[i * 4 + 2] = alarms[i].days;
    b[i * 4 + 3] = alarms[i].on;
  }
  Lock l;
  prefs.begin("alarm", false);
  prefs.putBytes("a", b, alarmN * 4);
  prefs.putInt("n", alarmN);
  prefs.end();
}

static void alarmsLoad() {
  uint8_t b[24];
  Lock l;
  prefs.begin("alarm", true);
  alarmN = constrain(prefs.getInt("n", 0), 0, 6);
  size_t got = alarmN ? prefs.getBytes("a", b, sizeof b) : 0;
  prefs.end();
  if (got < (size_t)alarmN * 4) alarmN = got / 4;
  for (int i = 0; i < alarmN; i++) {
    alarms[i].h = b[i * 4] % 24;
    alarms[i].m = b[i * 4 + 1] % 60;
    alarms[i].days = b[i * 4 + 2] & 0x7F;
    alarms[i].on = b[i * 4 + 3];
  }
  alarmsLoaded = true;
}

static const char* const DAY_1[] = {"P", "S", "Ç", "P", "C", "C", "P"};  // Pzt…Paz
static String alarmDays(uint8_t d) {
  if (!d) return "Bir kez";
  if (d == 0x7F) return "Her gün";
  if (d == 0x1F) return "Hafta içi";
  if (d == 0x60) return "Hafta sonu";
  String s;
  const char* k[] = {"Pzt", "Sal", "Çar", "Per", "Cum", "Cmt", "Paz"};
  for (int i = 0; i < 7; i++)
    if (d & (1 << i)) s += String(s.length() ? " " : "") + k[i];
  return s;
}

// Geri sayım
static uint32_t cdSet = 5 * 60;  // saniye
static bool cdRunning = false;
static uint32_t cdEnd = 0, cdLeft = 5 * 60 * 1000UL;  // duraklatılınca kalan (ms)

// Kronometre
static bool cwRunning = false;
static uint32_t cwStart = 0, cwAcc = 0;
static std::vector<uint32_t> cwLaps;
static uint32_t cwElapsed() { return cwAcc + (cwRunning ? millis() - cwStart : 0); }

// Çalma
static bool ringOn = false;
static String ringTitle;
static uint32_t ringStart = 0, snoozeAt = 0;
static int ringLastMinute = -1;

// ============================================================================
// Dünya saatleri (yaz saati kuralları dahil)
// ============================================================================
struct City {
  const char* name;
  int16_t off;  // standart saat farkı (dakika, UTC'ye göre)
  uint8_t rule;  // 0 yok, 1 Avrupa, 2 ABD, 3 Avustralya
};
static const City CITIES[] = {
    {"Londra", 0, 1},       {"Berlin", 60, 1},     {"Moskova", 180, 0},    {"Dubai", 240, 0},
    {"Yeni Delhi", 330, 0}, {"Pekin", 480, 0},     {"Tokyo", 540, 0},      {"Sidney", 600, 3},
    {"New York", -300, 2},  {"Chicago", -360, 2},  {"Los Angeles", -480, 2}, {"São Paulo", -180, 0},
};
static const int CITY_N = sizeof(CITIES) / sizeof(CITIES[0]);

static long daysFromCivil(int y, int m, int d) {  // 1970-01-01'den gün
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}
static int weekdayOfDays(long days) { return (int)((days % 7 + 11) % 7); }  // 0 Pazar
// Ayın n'inci pazarı (n = -1: son)
static int sundayOf(int y, int m, int n) {
  if (n > 0) {
    int wd = weekdayOfDays(daysFromCivil(y, m, 1));
    return 1 + (7 - wd) % 7 + (n - 1) * 7;
  }
  int last = m == 2 ? 28 : (m == 4 || m == 6 || m == 9 || m == 11 ? 30 : 31);
  int wd = weekdayOfDays(daysFromCivil(y, m, last));
  return last - wd;
}
static time_t utcAt(int y, int m, int d, int hh, int mm = 0) { return (time_t)(daysFromCivil(y, m, d) * 86400L + hh * 3600L + mm * 60L); }

static int cityOffsetMin(const City& c, time_t now) {
  struct tm u;
  gmtime_r(&now, &u);
  int y = u.tm_year + 1900;
  bool dst = false;
  if (c.rule == 1) {
    dst = now >= utcAt(y, 3, sundayOf(y, 3, -1), 1) && now < utcAt(y, 10, sundayOf(y, 10, -1), 1);
  } else if (c.rule == 2) {
    time_t a = utcAt(y, 3, sundayOf(y, 3, 2), 2) - c.off * 60L;
    time_t b = utcAt(y, 11, sundayOf(y, 11, 1), 1) - c.off * 60L;
    dst = now >= a && now < b;
  } else if (c.rule == 3) {
    time_t endA = utcAt(y, 4, sundayOf(y, 4, 1), 2) - c.off * 60L;
    time_t startO = utcAt(y, 10, sundayOf(y, 10, 1), 2) - c.off * 60L;
    dst = now < endA || now >= startO;
  }
  return c.off + (dst ? 60 : 0);
}

// ============================================================================
// Arka plan: alarm ve sayaç (her döngüde, hangi ekran açık olursa olsun)
// ============================================================================
static void ringStartNow(const String& title) {
  ringOn = true;
  ringTitle = title;
  ringStart = millis();
  sysWake();
  shellRedraw(true);
}

static void clockBgTick() {
  static uint32_t last = 0;
  uint32_t ms = millis();
  if (ms - last < 250) return;
  last = ms;
  if (!alarmsLoaded) alarmsLoad();
  if (cdRunning && (int32_t)(ms - cdEnd) >= 0) {
    cdRunning = false;
    cdLeft = cdSet * 1000UL;
    ringStartNow("Süre doldu");
  }
  if (snoozeAt && (int32_t)(ms - snoozeAt) >= 0) {
    snoozeAt = 0;
    ringStartNow("Alarm (ertelendi)");
  }
  struct tm t;
  if (localNow(t)) {
    int minute = t.tm_hour * 60 + t.tm_min;
    if (minute != ringLastMinute) {
      ringLastMinute = minute;
      int wd = (t.tm_wday + 6) % 7;  // 0 Pazartesi
      for (int i = 0; i < alarmN; i++) {
        Alarm& a = alarms[i];
        if (!a.on || a.h != t.tm_hour || a.m != t.tm_min) continue;
        if (a.days && !(a.days & (1 << wd))) continue;
        if (!a.days) {
          a.on = false;  // bir kezlik
          alarmsSave();
        }
        ringStartNow("Alarm " + fmtHM(a.h, a.m));
        break;
      }
    }
  }
  if (ringOn) {
    bool ph = ((ms - ringStart) / 450) % 2;
    sysLed(ph ? 90 : 0, ph ? 90 : 0, ph ? 110 : 0);
    sysSetBacklight(ph ? 255 : 90);
    if (ms - ringStart > 5UL * 60UL * 1000UL) {  // 5 dakika sonra kendiliğinden susar
      ringOn = false;
      sysSetBacklight(cfg.bright);
      shellRedraw(true);
    }
  }
}

static void ringDraw() {
  fillBg(0, 0, SCR_W, SCR_H);
  drawIcon(I_ALARM, 160 - 20, 20, 40, C_ACC, C_BG);
  bigText(clockStr(), (SCR_W - SPR_W) / 2, 72, SPR_W, 50, C_TEXT, C_BG, TC_DATUM);
  text(ringTitle, 160, 142, F_MD, C_TEXT, C_BG, MC_DATUM);
  button(16, 182, 140, 42, "Ertele 5 dk", B_NORMAL, I_REFRESH);
  button(164, 182, 140, 42, "Kapat", B_PRIMARY, I_CLOSE);
}

static void ringTap(int x, int y) {
  if (y < 176) return;
  ringOn = false;
  sysSetBacklight(cfg.bright);
  if (x < 160) {
    snoozeAt = millis() + 5UL * 60UL * 1000UL;
    toast("5 dakika sonra yeniden çalacak");
  }
  shellRedraw(true);
}

// ============================================================================
// Arayüz
// ============================================================================
static uint8_t clkTab = 0;  // 0 Alarm, 1 Sayaç, 2 Kronometre, 3 Dünya
static int clkEdit = -1;    // düzenlenen alarm (-1: yok)
static Alarm clkDraft;
static bool clkCities = false;  // şehir seçimi sayfası
static int clkCityFirst = 0;
static const char* const CLK_TABS[] = {"Alarm", "Sayaç", "Krono", "Dünya"};

// --- Alarm listesi ---
static const int AL_ROW = 42;
static void clkDrawAlarms() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  for (int i = 0; i < alarmN; i++) {
    int y = HDR_H + i * AL_ROW;
    const Alarm& a = alarms[i];
    text(fmtHM(a.h, a.m), 12, y + 8, F_LG, a.on ? C_TEXT : C_TEXT3, C_BG);
    text(alarmDays(a.days), 96, y + AL_ROW / 2 + 1, F_SM, a.on ? C_TEXT2 : C_TEXT3, C_BG, ML_DATUM);
    toggleSw(272, y + 11, a.on, C_BG);
    hline(12, y + AL_ROW - 1, 296);
  }
  if (alarmN < 6) {
    int y = HDR_H + alarmN * AL_ROW;
    drawIcon(I_PLUS, 12, y + 12, 18, C_ACC, C_BG);
    text("Alarm ekle", 40, y + AL_ROW / 2 + 1, F_SM, C_TEXT2, C_BG, ML_DATUM);
  }
  if (!alarmN) {
    auto ls = wrapText("Cihazda hoparlör olmadığı için alarm çalınca ekran yanıp söner ve ışık yanar.", F_XS, 296, 3);
    for (size_t i = 0; i < ls.size(); i++) text(ls[i], 12, HDR_H + 60 + i * 15, F_XS, C_TEXT3, C_BG);
  }
}

// --- Alarm düzenleme ---
// Saat ve dakika ayrı ayrı: üstteki ok artırır, alttaki azaltır. Gün seçilmezse bir kez çalar.
static const int AE_UP = HDR_H + 4, AE_DIG = HDR_H + 37, AE_DN = HDR_H + 88, AE_DAYS = HDR_H + 146;
static void clkDrawEditDigits() {
  bigText(String(clkDraft.h / 10) + String(clkDraft.h % 10), 30, AE_DIG, 100, 50, C_TEXT, C_BG, TC_DATUM);
  bigText(":", 140, AE_DIG, 40, 50, C_TEXT3, C_BG, TC_DATUM);
  bigText(String(clkDraft.m / 10) + String(clkDraft.m % 10), 190, AE_DIG, 100, 50, C_TEXT, C_BG, TC_DATUM);
}
static void clkDrawEdit() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  iconButton(63, AE_UP, 32, I_UP);
  iconButton(223, AE_UP, 32, I_UP);
  clkDrawEditDigits();
  iconButton(63, AE_DN, 32, I_DOWN);
  iconButton(223, AE_DN, 32, I_DOWN);
  text(alarmDays(clkDraft.days), 160, AE_DN + 37, F_XS, C_TEXT2, C_BG, TC_DATUM);
  for (int i = 0; i < 7; i++) {
    bool on = clkDraft.days & (1 << i);
    button(12 + i * 42, AE_DAYS, 38, 28, DAY_1[i], on ? B_ON : B_NORMAL);
  }
  button(12, 208, 90, 30, clkEdit < alarmN ? "Sil" : "Vazgeç", B_NORMAL, clkEdit < alarmN ? I_TRASH : I_NONE);
  button(110, 208, 198, 30, "Kaydet", B_PRIMARY, I_CHECK);
}

// --- Sayaç ---
static const uint16_t CD_PRESETS[] = {60, 180, 300, 600, 900, 1500, 1800, 2700};
static uint32_t cdRemain() {
  if (cdRunning) return (int32_t)(cdEnd - millis()) > 0 ? cdEnd - millis() : 0;
  return cdLeft;
}
static void clkDrawCdDigits() {
  uint32_t s = (cdRemain() + 999) / 1000;
  String t = s >= 3600 ? fmtClock(s) : fmtHM(s / 60, s % 60);
  bigText(t, (SCR_W - SPR_W) / 2, HDR_H + 10, SPR_W, 50, cdRunning ? C_TEXT : C_TEXT2, C_BG, TC_DATUM);
  int pct = cdSet ? (int)((uint64_t)(cdSet * 1000UL - cdRemain()) * 100 / (cdSet * 1000UL)) : 0;
  progress(12, HDR_H + 66, 296, 2, pct, C_ACC);
}
static void clkDrawCountdown() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  clkDrawCdDigits();
  bool idle = !cdRunning && cdLeft == cdSet * 1000UL;
  if (idle) {
    for (int i = 0; i < 8; i++) {
      int x = 12 + (i % 4) * 75, y = HDR_H + 74 + (i / 4) * 32;
      String l = CD_PRESETS[i] >= 60 ? String(CD_PRESETS[i] / 60) + " dk" : String(CD_PRESETS[i]) + " sn";
      button(x, y, 71, 28, l, cdSet == CD_PRESETS[i] ? B_ON : B_NORMAL);
    }
    const char* adj[] = {"−1 dk", "+1 dk", "−10 sn", "+10 sn"};
    for (int i = 0; i < 4; i++) button(12 + i * 75, HDR_H + 138, 71, 28, adj[i], B_NORMAL);
  }
  button(12, 202, 196, 34, cdRunning ? "Duraklat" : (idle ? "Başlat" : "Devam et"), B_PRIMARY, cdRunning ? I_PAUSE : I_PLAY);
  button(216, 202, 92, 34, "Sıfırla", idle ? B_DISABLED : B_NORMAL);
}

// --- Kronometre ---
static String cwFmt(uint32_t ms, bool tenth = true) {
  uint32_t s = ms / 1000;
  String r = s >= 3600 ? fmtClock(s) : fmtHM(s / 60, s % 60);
  if (tenth) r += "," + String((ms / 100) % 10);
  return r;
}
static void clkDrawCwDigits() {
  uint32_t e = cwElapsed();
  uint32_t s = e / 1000;
  String t = s >= 3600 ? fmtClock(s) : fmtHM(s / 60, s % 60);
  int w = bigW(t);
  int x = 160 - (w + 30) / 2;
  bigText(t, x, HDR_H + 10, w + 2, 50, cwRunning ? C_TEXT : C_TEXT2, C_BG, TL_DATUM);
  tft.fillRect(x + w + 2, HDR_H + 10, 40, 50, C_BG);
  text("," + String((e / 100) % 10), x + w + 2, HDR_H + 32, F_LG, C_TEXT2, C_BG);
}
static void clkDrawLaps() {
  fillBg(0, HDR_H + 70, SCR_W, 116);
  int n = cwLaps.size();
  for (int i = 0; i < std::min(n, 5); i++) {
    int k = n - 1 - i;
    uint32_t prev = k ? cwLaps[k - 1] : 0;
    int y = HDR_H + 72 + i * 23;
    text("Tur " + String(k + 1), 12, y + 11, F_SM, C_TEXT2, C_BG, ML_DATUM);
    text(cwFmt(cwLaps[k] - prev), 160, y + 11, F_SM, C_TEXT, C_BG, MC_DATUM);
    text(cwFmt(cwLaps[k]), 308, y + 11, F_XS, C_TEXT3, C_BG, MR_DATUM);
    hline(12, y + 22, 296);
  }
}
static void clkDrawStopwatch() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  clkDrawCwDigits();
  clkDrawLaps();
  bool started = cwElapsed() > 0;
  button(12, 196, 196, 38, cwRunning ? "Durdur" : (started ? "Devam et" : "Başlat"), B_PRIMARY, cwRunning ? I_PAUSE : I_PLAY);
  button(216, 196, 92, 38, cwRunning ? "Tur" : "Sıfırla", started ? B_NORMAL : B_DISABLED);
}

// --- Dünya ---
static void clkDrawWorld() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  if (clkCities) {
    for (int r = 0; r < 6; r++) {
      int i = clkCityFirst + r, y = HDR_H + r * 30;
      if (i >= CITY_N) break;
      bool on = cfg.worldMask & (1 << i);
      text(CITIES[i].name, 12, y + 15, F_SM, C_TEXT, C_BG, ML_DATUM);
      toggleSw(272, y + 5, on, C_BG);
      hline(12, y + 29, 296);
    }
    button(12, 212 - 4, 296, 30, "Bitti", B_PRIMARY);
    return;
  }
  time_t now = time(nullptr);
  int myOff = 180;  // cihaz saati (UTC+3)
  int row = 0;
  for (int i = 0; i < CITY_N && row < 4; i++) {
    if (!(cfg.worldMask & (1 << i))) continue;
    int off = cityOffsetMin(CITIES[i], now);
    time_t lt = now + off * 60;
    struct tm t;
    gmtime_r(&lt, &t);
    int y = HDR_H + row * 42;
    int diff = off - myOff;
    String d = diff == 0 ? String("Aynı saat") : String(abs(diff) / 60) + (abs(diff) % 60 ? ",5" : "") + (diff > 0 ? " sa ileri" : " sa geri");
    text(CITIES[i].name, 12, y + 4, F_MD, C_TEXT, C_BG);
    struct tm me;
    String dayTag;
    if (localNow(me) && me.tm_mday != t.tm_mday) dayTag = (diff > 0 ? " · yarın" : " · dün");
    text(d + dayTag, 12, y + 25, F_XS, C_TEXT2, C_BG);
    text(fmtHM(t.tm_hour, t.tm_min), 308, y + 8, F_LG, C_TEXT, C_BG, TR_DATUM);
    hline(12, y + 41, 296);
    row++;
  }
  int y = HDR_H + row * 42;
  drawIcon(I_GLOBE, 12, y + 12, 18, C_ACC, C_BG);
  text("Şehir seç", 40, y + 21, F_SM, C_TEXT2, C_BG, ML_DATUM);
  if (!timeOk()) text("Saat henüz ayarlanmadı", 160, 220, F_XS, C_TEXT3, C_BG, MC_DATUM);
}

static void clkDrawBody() {
  if (clkEdit >= 0) clkDrawEdit();
  else if (clkTab == 0) clkDrawAlarms();
  else if (clkTab == 1) clkDrawCountdown();
  else if (clkTab == 2) clkDrawStopwatch();
  else clkDrawWorld();
}

static void clkDraw(bool full) {
  if (!alarmsLoaded) alarmsLoad();
  if (full) {
    if (clkEdit >= 0) {
      header(clkEdit < alarmN ? "Alarmı düzenle" : "Yeni alarm");
    } else {
      header("Saat", true, false);
      segmented(88, 3, 224, 23, CLK_TABS, 4, clkTab);
    }
  }
  clkDrawBody();
}

static void clkSetTab(int t) {
  clkTab = t;
  clkCities = false;
  clkDraw(true);
}

static void clkTap(int x, int y) {
  if (y < HDR_H) {
    if (clkEdit < 0 && x >= 88) clkSetTab(segmentedHit(x, 88, 224, 4));
    return;
  }
  if (clkEdit >= 0) {
    Alarm& a = clkDraft;
    bool hourCol = x < 160;
    if (y >= AE_UP && y < AE_UP + 34) {
      if (hourCol) a.h = (a.h + 1) % 24;
      else a.m = (a.m + 1) % 60;
    } else if (y >= AE_DIG && y < AE_DIG + 50) {
      if (hourCol) a.h = (a.h + 1) % 24;
      else a.m = (a.m + 5) % 60 - (a.m + 5) % 5;  // dakikaya dokununca beşer
    } else if (y >= AE_DN && y < AE_DN + 34) {
      if (hourCol) a.h = (a.h + 23) % 24;
      else a.m = (a.m + 59) % 60;
    } else if (y >= AE_DAYS - 2 && y < AE_DAYS + 32) {
      int i = constrain((x - 12) / 42, 0, 6);
      a.days ^= (1 << i);
      clkDrawEdit();
      return;
    } else if (y >= 206) {
      if (x < 106) {
        if (clkEdit < alarmN) {
          for (int i = clkEdit; i < alarmN - 1; i++) alarms[i] = alarms[i + 1];
          alarmN--;
          alarmsSave();
        }
      } else {
        a.on = true;
        if (clkEdit >= alarmN) alarmN++;
        alarms[clkEdit] = a;
        alarmsSave();
        toast("Alarm " + fmtHM(a.h, a.m) + " · " + alarmDays(a.days));
      }
      clkEdit = -1;
      clkDraw(true);
      return;
    } else {
      return;
    }
    clkDrawEditDigits();
    return;
  }
  if (clkTab == 0) {
    int i = (y - HDR_H) / AL_ROW;
    if (i < alarmN) {
      if (x >= 262) {
        alarms[i].on = !alarms[i].on;
        alarmsSave();
        toggleSw(272, HDR_H + i * AL_ROW + 11, alarms[i].on, C_BG);
        clkDrawAlarms();
      } else {
        clkEdit = i;
        clkDraft = alarms[i];
        clkDraw(true);
      }
    } else if (i == alarmN && alarmN < 6) {
      clkEdit = alarmN;
      clkDraft = Alarm();
      struct tm t;
      if (localNow(t)) {
        clkDraft.h = (t.tm_hour + 1) % 24;
        clkDraft.m = 0;
      }
      clkDraw(true);
    }
  } else if (clkTab == 1) {
    bool idle = !cdRunning && cdLeft == cdSet * 1000UL;
    if (idle && y >= HDR_H + 72 && y < HDR_H + 136) {
      int i = constrain((x - 12) / 75, 0, 3) + constrain((y - HDR_H - 74) / 32, 0, 1) * 4;
      cdSet = CD_PRESETS[i];
    } else if (idle && y >= HDR_H + 136 && y < HDR_H + 168) {
      int k = constrain((x - 12) / 75, 0, 3);
      int d[] = {-60, 60, -10, 10};
      cdSet = constrain((int)cdSet + d[k], 10, 99 * 60);
    } else if (y >= 198) {
      if (x < 212) {
        if (cdRunning) {
          cdLeft = cdRemain();
          cdRunning = false;
        } else {
          cdEnd = millis() + cdLeft;
          cdRunning = true;
        }
      } else if (!idle) {
        cdRunning = false;
        cdLeft = cdSet * 1000UL;
      }
    } else {
      return;
    }
    if (!cdRunning && !(cdLeft < cdSet * 1000UL)) cdLeft = cdSet * 1000UL;
    clkDrawCountdown();
  } else if (clkTab == 2) {
    if (y < 192) return;
    if (x < 212) {
      if (cwRunning) {
        cwAcc = cwElapsed();
        cwRunning = false;
      } else {
        cwStart = millis();
        cwRunning = true;
      }
    } else if (cwRunning) {
      if (cwLaps.size() < 99) cwLaps.push_back(cwElapsed());
    } else {
      cwAcc = 0;
      cwLaps.clear();
    }
    clkDrawStopwatch();
  } else {
    if (clkCities) {
      if (y >= 206) {
        clkCities = false;
        saveConfig();
      } else {
        int i = clkCityFirst + (y - HDR_H) / 30;
        if (i < CITY_N) cfg.worldMask ^= (1 << i);
      }
      clkDrawWorld();
      return;
    }
    int shown = 0;
    for (int i = 0; i < CITY_N; i++)
      if (cfg.worldMask & (1 << i)) shown++;
    shown = std::min(shown, 4);
    if (y >= HDR_H + shown * 42 && y < HDR_H + shown * 42 + 42) {
      clkCities = true;
      clkCityFirst = 0;
      clkDrawWorld();
    }
  }
}

static void clkSwipe(int dir) {
  if (clkEdit >= 0) return;
  if (clkTab == 3 && clkCities && (dir == SW_UP || dir == SW_DOWN)) {
    clkCityFirst = constrain(clkCityFirst + (dir == SW_UP ? 5 : -5), 0, CITY_N - 6);
    clkDrawWorld();
  } else if (dir == SW_LEFT && clkTab < 3) {
    clkSetTab(clkTab + 1);
  } else if (dir == SW_RIGHT && clkTab > 0) {
    clkSetTab(clkTab - 1);
  }
}

static void clkTick() {
  static uint32_t last = 0;
  uint32_t ms = millis();
  if (clkEdit >= 0) return;
  if (clkTab == 1 && cdRunning && ms - last >= 250) {
    last = ms;
    clkDrawCdDigits();
  } else if (clkTab == 2 && cwRunning && ms - last >= 100) {
    last = ms;
    clkDrawCwDigits();
  } else if (clkTab == 3 && !clkCities && ms - last >= 15000) {
    last = ms;
    clkDrawWorld();
  } else if (clkTab == 1 && !cdRunning && cdLeft == cdSet * 1000UL && ms - last > 400) {
    last = ms;  // süre dolunca ekran tazelensin
  }
}

static bool clkBack() {
  if (clkEdit >= 0) {
    clkEdit = -1;
    clkDraw(true);
    return true;
  }
  if (clkCities) {
    clkCities = false;
    saveConfig();
    clkDrawWorld();
    return true;
  }
  return false;
}

static void clkOpen() {
  clkEdit = -1;
  clkCities = false;
  if (cdRunning) clkTab = 1;
  else if (cwRunning) clkTab = 2;
}

static const App APP_CLOCK = {"Saat", "Saat", I_ALARM, clkOpen, clkDraw, clkTap, nullptr, clkSwipe, nullptr, clkTick, clkBack, nullptr};
