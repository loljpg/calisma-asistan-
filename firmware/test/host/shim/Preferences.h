#pragma once
#include "Arduino.h"
#include <map>
#include <string>
// Ev önizlemesi: kalıcı ayarlar bellekte.
class Preferences {
 public:
  bool begin(const char* ns, bool = false) { _ns = ns; return true; }
  void end() {}
  bool remove(const char* k) { db()[key(k)].clear(); db().erase(key(k)); return true; }
  bool clear() { return true; }
  bool isKey(const char* k) { return db().count(key(k)); }
  size_t putString(const char* k, const String& v) { db()[key(k)] = std::string(v.c_str()); return v.length(); }
  String getString(const char* k, const String& d = String()) { auto it = db().find(key(k)); return it == db().end() ? d : String(it->second.c_str()); }
  size_t putBool(const char* k, bool v) { db()[key(k)] = v ? "1" : "0"; return 1; }
  bool getBool(const char* k, bool d = false) { auto it = db().find(key(k)); return it == db().end() ? d : it->second == "1"; }
  size_t putInt(const char* k, int32_t v) { db()[key(k)] = std::to_string(v); return 4; }
  int32_t getInt(const char* k, int32_t d = 0) { auto it = db().find(key(k)); return it == db().end() ? d : atoi(it->second.c_str()); }
  size_t putUInt(const char* k, uint32_t v) { db()[key(k)] = std::to_string(v); return 4; }
  uint32_t getUInt(const char* k, uint32_t d = 0) { auto it = db().find(key(k)); return it == db().end() ? d : strtoul(it->second.c_str(), 0, 10); }
  size_t putUChar(const char* k, uint8_t v) { return putUInt(k, v); }
  uint8_t getUChar(const char* k, uint8_t d = 0) { return getUInt(k, d); }
  size_t putShort(const char* k, int16_t v) { return putInt(k, v); }
  int16_t getShort(const char* k, int16_t d = 0) { return getInt(k, d); }
  size_t putFloat(const char* k, float v) { db()[key(k)] = std::to_string(v); return 4; }
  float getFloat(const char* k, float d = 0) { auto it = db().find(key(k)); return it == db().end() ? d : atof(it->second.c_str()); }
  size_t putBytes(const char* k, const void* v, size_t n) { db()[key(k)] = std::string((const char*)v, n); return n; }
  size_t getBytesLength(const char* k) { auto it = db().find(key(k)); return it == db().end() ? 0 : it->second.size(); }
  size_t getBytes(const char* k, void* buf, size_t n) { auto it = db().find(key(k)); if (it == db().end()) return 0; size_t m = std::min(n, it->second.size()); memcpy(buf, it->second.data(), m); return m; }
 private:
  std::string key(const char* k) { return _ns + "/" + k; }
  static std::map<std::string, std::string>& db() { static std::map<std::string, std::string> m; return m; }
  std::string _ns;
};
