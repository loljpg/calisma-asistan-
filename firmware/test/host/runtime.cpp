#include <cstdlib>
// Ev önizlemesi: Arduino ve SD yerine geçenlerin gövdeleri.
#include <Arduino.h>
#include <SD.h>
#include <stdarg.h>
#include <random>
uint32_t g_ms = 1000;
bool g_ms_hook = false;
uint32_t millis() {
  if (g_ms_hook) g_ms += 1;
  return g_ms;
}
uint32_t micros() { return g_ms * 1000; }
void delay(uint32_t ms) { g_ms += ms; }
void delayMicroseconds(uint32_t) {}
static std::mt19937 rng(42);
uint32_t esp_random(void) { return rng(); }
long random(long b) { return b ? rng() % b : 0; }
long random(long a, long b) { return a + random(b - a); }
int g_pins[64];
int digitalRead(uint8_t p) { return p == 0 ? 1 : g_pins[p]; }
void digitalWrite(uint8_t p, uint8_t v) { g_pins[p] = v; }
void pinMode(uint8_t, uint8_t) {}
uint16_t g_ldr = 1800;
uint16_t analogRead(uint8_t) { return g_ldr; }
void ledcWrite(uint8_t, uint32_t) {}
float temperatureRead() { return 47.5f; }
HostSerial Serial;
void HostSerial::printf(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
HostEsp ESP;
SPIClass SPI;
SDFS SD;
// SD kartın bilgisayardaki karşılığı: DAXEN_SD ortam değişkeni (run.sh geçici bir klasör verir)
std::string g_sdroot = getenv("DAXEN_SD") ? getenv("DAXEN_SD") : "/tmp/sdroot";
std::string g_outdir = getenv("DAXEN_OUT") ? getenv("DAXEN_OUT") : "out";
namespace fs {
std::string hostPath(const char* p) { std::string s = g_sdroot; if (!p || p[0] != '/') s += "/"; s += p ? p : ""; return s; }
File FS::open(const char* path, const char* mode, bool) {
  std::string hp = hostPath(path);
  struct stat st;
  if (!strcmp(mode, "r") && stat(hp.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return File(path, nullptr, opendir(hp.c_str()));
  FILE* f = fopen(hp.c_str(), !strcmp(mode, "w") ? "wb" : (!strcmp(mode, "a") ? "ab" : "rb"));
  if (!f) return File();
  return File(path, f, nullptr);
}
File File::openNextFile(const char*) {
  if (!_d) return File();
  struct dirent* e;
  while ((e = readdir(_d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    std::string vp = _vpath == "/" ? "/" + std::string(e->d_name) : _vpath + "/" + e->d_name;
    std::string hp = hostPath(vp.c_str());
    struct stat st;
    if (stat(hp.c_str(), &st)) continue;
    if (S_ISDIR(st.st_mode)) return File(vp, nullptr, opendir(hp.c_str()));
    return File(vp, fopen(hp.c_str(), "rb"), nullptr);
  }
  return File();
}
}  // namespace fs
extern "C" char* itoa(int value, char* result, int base) {
  if (base < 2 || base > 36) { *result = 0; return result; }
  char* out = result; int quotient = abs(value);
  do { const int tmp = quotient / base; *out = "0123456789abcdefghijklmnopqrstuvwxyz"[quotient - (tmp * base)]; ++out; quotient = tmp; } while (quotient);
  if (value < 0) *out++ = '-';
  std::reverse(result, out); *out = 0; return result;
}
extern "C" char* utoa(unsigned value, char* result, int base) {
  if (base < 2 || base > 36) { *result = 0; return result; }
  char* out = result; unsigned quotient = value;
  do { const unsigned tmp = quotient / base; *out = "0123456789abcdefghijklmnopqrstuvwxyz"[quotient - (tmp * base)]; ++out; quotient = tmp; } while (quotient);
  std::reverse(result, out); *out = 0; return result;
}
