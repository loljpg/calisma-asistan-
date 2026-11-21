#pragma once
// DAXEN VANE · Soru sayacı ve çalışma geçmişi hesapları (saf; bilgisayarda sınanır). Kullanımı
// apps/yks.h: günde çözülen soru (toplam ve derse göre, son 14 gün), kronometre oturumlarından
// günlük çalışma dakikası (kalıcı kayıt satırlarından).
#include <stdint.h>
#include <string.h>

#define SQ_DAYS 14
#define SQ_DERS 12
static const char* const SQ_DERS_AD[SQ_DERS] = {"Türkçe", "Matematik", "Geometri", "Fizik",  "Kimya", "Biyoloji",
                                                "Tarih",  "Coğrafya",  "Felsefe",  "Din",    "Edebiyat", "Diğer"};

struct SoruLog {
  uint32_t day = 0;              // bugünün gün numarası (1970-01-01 = 0)
  uint16_t last[SQ_DAYS] = {0};  // last[0] bugün, last[i] i gün önce
  uint16_t ders[SQ_DERS] = {0};  // bugün, derse göre
  uint16_t goal = 100;           // günlük hedef
};

// Takvim gününün numarası (Howard Hinnant, days_from_civil)
static int32_t sqDayNum(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}
// "AAAA-AA-GG" → gün numarası; bozuksa -1
static int32_t sqDayOf(const char* s) {
  if (!s || strlen(s) < 10 || s[4] != '-' || s[7] != '-') return -1;
  int y = 0, m = 0, d = 0;
  for (int i = 0; i < 4; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    y = y * 10 + (s[i] - '0');
  }
  for (int i = 5; i < 7; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    m = m * 10 + (s[i] - '0');
  }
  for (int i = 8; i < 10; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    d = d * 10 + (s[i] - '0');
  }
  if (m < 1 || m > 12 || d < 1 || d > 31) return -1;
  return sqDayNum(y, m, d);
}
// Haftanın günü: 0 pazartesi … 6 pazar
static int sqWeekday(int32_t day) { return (int)(((day % 7) + 7 + 3) % 7); }  // 1970-01-01 perşembe

// Yeni güne geçiş: eski günler kayar, derse göre sayılar sıfırlanır
static void sqRoll(SoruLog& s, uint32_t today) {
  if (s.day == today) return;
  if (s.day == 0 || today < s.day || today - s.day >= SQ_DAYS) {
    memset(s.last, 0, sizeof s.last);
  } else {
    uint32_t d = today - s.day;
    memmove(s.last + d, s.last, (SQ_DAYS - d) * sizeof s.last[0]);
    memset(s.last, 0, d * sizeof s.last[0]);
  }
  memset(s.ders, 0, sizeof s.ders);
  s.day = today;
}

// n soru ekler (eksi: düzeltme; sayılar sıfırın altına inmez). Döner: gerçekten eklenen.
static int sqAdd(SoruLog& s, uint32_t today, int ders, int n) {
  sqRoll(s, today);
  if (ders < 0 || ders >= SQ_DERS) ders = SQ_DERS - 1;
  int v = (int)s.ders[ders] + n;
  if (v < 0) v = 0;
  if (v > 9999) v = 9999;
  int real = v - s.ders[ders];
  s.ders[ders] = v;
  int t = (int)s.last[0] + real;
  s.last[0] = t < 0 ? 0 : (t > 65000 ? 65000 : t);
  return real;
}

static int sqSum(const SoruLog& s, int days) {
  int t = 0;
  for (int i = 0; i < days && i < SQ_DAYS; i++) t += s.last[i];
  return t;
}

// Ders adını listedekine eşler ("TYT Matematik" → Matematik); bulunamazsa -1
static int sqDersOf(const char* name) {
  if (!name || !*name) return -1;
  for (int i = 0; i < SQ_DERS - 1; i++)
    if (strstr(name, SQ_DERS_AD[i])) return i;
  return -1;
}

// Kalıcı kayıt satırı (JSON): kronometre oturumuysa gününü ve saniyesini verir
static bool histParseSession(const char* line, int32_t& day, int32_t& sec) {
  if (!strstr(line, "\"type\":\"session\"")) return false;
  const char* d = strstr(line, "\"date\":\"");
  const char* s = strstr(line, "\"sec\":");
  if (!d || !s) return false;
  day = sqDayOf(d + 8);
  sec = 0;
  for (s += 6; *s >= '0' && *s <= '9'; s++) sec = sec * 10 + (*s - '0');
  return day >= 0 && sec > 0 && sec <= 6 * 3600;
}

// Isı haritası düzeyi (0 boş … 4 çok)
static int histLevel(int minutes) { return minutes <= 0 ? 0 : minutes < 30 ? 1 : minutes < 90 ? 2 : minutes < 180 ? 3 : 4; }

// Bugünden geriye kesintisiz çalışılan gün sayısı ve en uzun seri (min: o gün en az kaç dakika)
static void histStreaks(const uint16_t* minutes, int n, int& cur, int& best) {
  cur = best = 0;
  int run = 0;
  bool open = true;  // bugünden başlayan seri
  for (int i = 0; i < n; i++) {  // minutes[0] bugün
    if (minutes[i] > 0) {
      run++;
      if (open) cur = run;
    } else {
      if (i > 0) open = false;  // bugün henüz çalışılmadıysa dünden sayılır
      run = 0;
    }
    if (run > best) best = run;
  }
}
