#pragma once
// Saat, biçimlendirme ve UTF-8 yardımcıları (donanımdan bağımsız).
#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

static bool timeOk() { return time(nullptr) > 1700000000; }

static bool localNow(struct tm& t) {
  if (!timeOk()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

static String todayStr() {
  struct tm t;
  if (!localNow(t)) return "";
  char b[11];
  strftime(b, sizeof b, "%Y-%m-%d", &t);
  return String(b);
}

static long long nowMs() {
  if (!timeOk()) return 0;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static int minutesOfDay() {
  struct tm t;
  if (!localNow(t)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

static int toMin(const String& hhmm) {
  int c = hhmm.indexOf(':');
  if (c < 0) return 0;
  return hhmm.substring(0, c).toInt() * 60 + hhmm.substring(c + 1).toInt();
}

static String fmtMin(int m) {
  if (m < 0) m = 0;
  if (m < 60) return String(m) + " dk";
  int h = m / 60, r = m % 60;
  return r ? String(h) + " sa " + String(r) + " dk" : String(h) + " sa";
}

static String fmtClock(uint32_t sec) {
  char b[20];
  uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (h) snprintf(b, sizeof b, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  else snprintf(b, sizeof b, "%02u:%02u", (unsigned)m, (unsigned)s);
  return String(b);
}

static String fmtMs(uint32_t ms) {
  uint32_t s = ms / 1000;
  char b[12];
  snprintf(b, sizeof b, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return String(b);
}

static String fmtHM(int h, int m) {
  char b[6];
  snprintf(b, sizeof b, "%02d:%02d", h, m);
  return String(b);
}

static String fmtBytes(uint64_t b) {
  if (b < 1024) return String((unsigned)b) + " B";
  if (b < 1024ULL * 1024) return String((float)b / 1024.f, b < 10240 ? 1 : 0) + " KB";
  if (b < 1024ULL * 1024 * 1024) return String((float)b / 1048576.f, b < 10485760ULL ? 1 : 0) + " MB";
  return String((float)b / 1073741824.f, 1) + " GB";
}

static String normalizeDbUrl(String s) {
  s.trim();
  while (s.endsWith("/")) s.remove(s.length() - 1);
  if (s.length() && !s.startsWith("https://")) {
    if (s.startsWith("http://")) s = "https://" + s.substring(7);
    else s = "https://" + s;
  }
  return s;
}

static const char* const GUN_ADI[] = {"Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi"};
static const char* const GUN_KISA[] = {"Paz", "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt"};
static const char* const AY_ADI[] = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
                                     "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};

static uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }

// --- UTF-8 ---
// s[i]'den başlayan karakterin bayt uzunluğu
static int utf8Len(uint8_t c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

// Son karakteri siler
static String utf8Pop(const String& s) {
  int i = (int)s.length() - 1;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
  return s.substring(0, i < 0 ? 0 : i);
}

// i bayt konumundan önceki karakterin başlangıcı
static int utf8Prev(const String& s, int i) {
  if (i <= 0) return 0;
  i--;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
  return i;
}

static int utf8Next(const String& s, int i) {
  if (i >= (int)s.length()) return s.length();
  return std::min((int)s.length(), i + utf8Len((uint8_t)s[i]));
}

static int utf8Count(const String& s) {
  int n = 0;
  for (size_t i = 0; i < s.length(); i++)
    if (((uint8_t)s[i] & 0xC0) != 0x80) n++;
  return n;
}

// Türkçe büyük harf (i→İ, ı→I, ç→Ç ...)
static String trUpper(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length();) {
    uint8_t c = s[i];
    int n = utf8Len(c);
    String ch = s.substring(i, i + n);
    i += n;
    if (ch == "i") o += "İ";
    else if (ch == "ı") o += "I";
    else if (ch == "ç") o += "Ç";
    else if (ch == "ğ") o += "Ğ";
    else if (ch == "ö") o += "Ö";
    else if (ch == "ş") o += "Ş";
    else if (ch == "ü") o += "Ü";
    else if (n == 1 && c >= 'a' && c <= 'z') o += (char)(c - 32);
    else o += ch;
  }
  return o;
}

static String trLower(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length();) {
    uint8_t c = s[i];
    int n = utf8Len(c);
    String ch = s.substring(i, i + n);
    i += n;
    if (ch == "İ") o += "i";
    else if (c == 'I' && n == 1) o += "ı";
    else if (ch == "Ç") o += "ç";
    else if (ch == "Ğ") o += "ğ";
    else if (ch == "Ö") o += "ö";
    else if (ch == "Ş") o += "ş";
    else if (ch == "Ü") o += "ü";
    else if (n == 1 && c >= 'A' && c <= 'Z') o += (char)(c + 32);
    else o += ch;
  }
  return o;
}

// Dosya adı için güvenli hâle getirir (Türkçe harfler kalır, / \ : * ? " < > | çıkar)
static String safeName(String s) {
  s.trim();
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (strchr("/\\:*?\"<>|", c) || (uint8_t)c < 32) o += '-';
    else o += c;
  }
  while (o.startsWith(".")) o.remove(0, 1);
  if (o.length() > 60) o = o.substring(0, 60);
  return o;
}

static String baseName(const String& p) {
  int s = p.lastIndexOf('/');
  return s < 0 ? p : p.substring(s + 1);
}

static String dirName(const String& p) {
  int s = p.lastIndexOf('/');
  return s <= 0 ? String("/") : p.substring(0, s);
}

static String extOf(const String& p) {
  String b = baseName(p);
  int d = b.lastIndexOf('.');
  if (d <= 0) return "";
  String e = b.substring(d + 1);
  e.toLowerCase();
  return e;
}

static String joinPath(const String& dir, const String& name) {
  if (dir.endsWith("/")) return dir + name;
  return dir + "/" + name;
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
