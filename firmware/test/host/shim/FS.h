#pragma once
// Ev önizlemesi: SD kart yerine bilgisayardaki bir klasör (SDROOT).
#include "Arduino.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
namespace fs {
std::string hostPath(const char* p);
class File {
 public:
  File() {}
  File(const std::string& vpath, FILE* f, DIR* d) : _vpath(vpath), _f(f), _d(d) {
    size_t s = vpath.find_last_of('/');
    _name = s == std::string::npos ? vpath : vpath.substr(s + 1);
  }
  operator bool() const { return _f || _d; }
  bool isDirectory() { return _d != nullptr; }
  const char* name() { return _name.c_str(); }
  const char* path() { return _vpath.c_str(); }
  size_t write(uint8_t c) { return _f ? fwrite(&c, 1, 1, _f) : 0; }
  size_t write(const uint8_t* b, size_t n) { return _f ? fwrite(b, 1, n, _f) : 0; }
  size_t print(const String& s) { return write((const uint8_t*)s.c_str(), s.length()); }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const String& s) { return print(s) + write('\n'); }
  size_t println(const char* s) { return print(s) + write('\n'); }
  int read() { if (!_f) return -1; int c = fgetc(_f); return c == EOF ? -1 : c; }
  size_t read(uint8_t* b, size_t n) { return _f ? fread(b, 1, n, _f) : 0; }
  size_t readBytes(char* b, size_t n) { return read((uint8_t*)b, n); }
  int peek() { if (!_f) return -1; int c = fgetc(_f); if (c != EOF) ungetc(c, _f); return c == EOF ? -1 : c; }
  int available() { if (!_f) return 0; long p = ftell(_f); return (int)(size() - p); }
  bool seek(uint32_t pos) { return _f && fseek(_f, pos, SEEK_SET) == 0; }
  size_t position() { return _f ? ftell(_f) : 0; }
  size_t size() { struct stat st; if (stat(hostPath(_vpath.c_str()).c_str(), &st)) return 0; return st.st_size; }
  void flush() { if (_f) fflush(_f); }
  time_t getLastWrite() { struct stat st; if (stat(hostPath(_vpath.c_str()).c_str(), &st)) return 0; return st.st_mtime; }
  String readStringUntil(char t) { String s; int c; while ((c = read()) >= 0 && c != t) s += (char)c; return s; }
  String readString() { String s; int c; while ((c = read()) >= 0) s += (char)c; return s; }
  void close() { if (_f) fclose(_f); if (_d) closedir(_d); _f = nullptr; _d = nullptr; }
  File openNextFile(const char* = "r");
  void rewindDirectory() { if (_d) rewinddir(_d); }
 private:
  std::string _vpath, _name;
  FILE* _f = nullptr;
  DIR* _d = nullptr;
};
class FS {
 public:
  File open(const char* path, const char* mode = "r", bool create = false);
  File open(const String& p, const char* mode = "r", bool create = false) { return open(p.c_str(), mode, create); }
  bool exists(const char* p) { struct stat st; return stat(hostPath(p).c_str(), &st) == 0; }
  bool exists(const String& p) { return exists(p.c_str()); }
  bool mkdir(const char* p) { return ::mkdir(hostPath(p).c_str(), 0755) == 0; }
  bool mkdir(const String& p) { return mkdir(p.c_str()); }
  bool remove(const char* p) { return ::unlink(hostPath(p).c_str()) == 0; }
  bool remove(const String& p) { return remove(p.c_str()); }
  bool rmdir(const char* p) { return ::rmdir(hostPath(p).c_str()) == 0; }
  bool rmdir(const String& p) { return rmdir(p.c_str()); }
  bool rename(const char* a, const char* b) { return ::rename(hostPath(a).c_str(), hostPath(b).c_str()) == 0; }
  bool rename(const String& a, const String& b) { return rename(a.c_str(), b.c_str()); }
};
}  // namespace fs
using fs::File;
