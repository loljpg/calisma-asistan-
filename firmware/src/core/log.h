#pragma once
// Sistem günlüğü: RAM'de 2 KB'lık halka (son satırlar, her zaman), cihazda ayrıca SD kopyası
// (sys/diag.h, /daxen/sys/gunluk.txt). Satır biçimi "SSSSS etiket: mesaj"; SSSSS açılıştan beri
// saniye. Satır en fazla LOG_LINE_MAX karakter. Şifre, PIN ya da anahtar günlüğe asla yazılmaz.
#include <vector>

#define LOG_RING 2048
#define LOG_LINE_MAX 120

struct LogRing {
  char buf[LOG_RING];
  size_t head = 0;     // sıradaki yazma konumu
  size_t used = 0;     // dolu bayt (en fazla LOG_RING)
  uint32_t lines = 0;  // açılıştan beri yazılan satır
};
static LogRing logRing;
static void (*logSink)(const char* line) = nullptr;  // SD kopyası (cihazda sys/diag.h bağlar)

// Halkaya bir satır ekler (sonuna '\n' koyar). Kilit çağırandadır.
static void logPut(LogRing& r, const char* s) {
  size_t n = strnlen(s, LOG_LINE_MAX);
  for (size_t i = 0; i <= n; i++) {
    char c = i < n ? s[i] : '\n';
    if (c == '\n' && i < n) c = ' ';  // satır içi yeni satır boşluk olur
    r.buf[r.head] = c;
    r.head = (r.head + 1) % LOG_RING;
    if (r.used < LOG_RING) r.used++;
  }
  r.lines++;
}

// Son en fazla max satır, eskiden yeniye. Halka dolmuşsa yarım kalan ilk satır atlanır.
static std::vector<String> logTail(const LogRing& r, size_t max) {
  std::vector<String> out;
  size_t start = (r.head + LOG_RING - r.used) % LOG_RING;
  size_t i = 0;
  if (r.used == LOG_RING) {
    while (i < r.used && r.buf[(start + i) % LOG_RING] != '\n') i++;
    i++;
  }
  String cur;
  for (; i < r.used; i++) {
    char c = r.buf[(start + i) % LOG_RING];
    if (c == '\n') {
      out.push_back(cur);
      cur = "";
    } else {
      cur += c;
    }
  }
  if (out.size() > max) out.erase(out.begin(), out.end() - max);
  return out;
}

// Günlüğe yazar: etiket kısa bir alan adıdır ("açılış", "ağ", "bt", "bellek", "güncelleme"…)
static void sysLog(const char* tag, const String& msg) {
  char line[LOG_LINE_MAX + 1];
  snprintf(line, sizeof line, "%5lu %s: %s", (unsigned long)(millis() / 1000), tag, msg.c_str());
  Lock l;
  logPut(logRing, line);
  if (logSink) logSink(line);
}
