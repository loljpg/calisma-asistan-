#pragma once
// Kendi uygulamaların: SD karttaki /daxen/uygulamalar/*.lua dosyaları başlatıcıda uygulama olarak
// görünür. Lua 5.4 (32 bit). Türkçe arabirim: ekran, renk, zaman, dosya, ayar, sistem.
// Ayrıntılar: SD kartta /daxen/uygulamalar/REHBER.txt ve depoda docs/lua.md.
//
// Her Lua uygulaması çekirdekte ayrı bir süreçtir (os/kernel.h): kendi yığını vardır, kesmeyle
// zaman dilimlenir. Arayüz dokunmaları ileti kuyruğuna koyar; süreç kuyruğu boşaltıp Lua
// işlevlerini çağırır ve ekrana Gfx kilidiyle çizer. Uygulama uzun hesap yapsa da saat, dokunma
// ve alarmlar donmaz; geri tuşu ya da Görevler uygulaması süreci her an sonlandırabilir.
#include "../os/shell.h"
#include "../os/kernel.h"
#include <SD.h>
#include <deque>
#include "lua.hpp"

#define LUA_APPS_DIR "/daxen/uygulamalar"

// İzinler ("-- izin: ekran, dosya"); satırı olmayan eski uygulamalar bugünkü API'lerin hepsini alır
enum : uint8_t { LP_EKRAN = 1, LP_DOSYA = 2, LP_LED = 4, LP_BILDIRIM = 8, LP_WIFI = 16, LP_BT = 32, LP_ESKI = 15 };
static const char* const LUA_PERMS[] = {"ekran", "dosya", "led", "bildirim", "wifi", "bluetooth"};

struct LuaAppInfo {
  String path, name, ver;
  uint8_t icon = I_CODE;
  uint8_t perm = LP_ESKI;
  bool bg = false;        // "-- arkaplan: evet": çıkınca kapanmaz, arka planda sürer
  bool manifest = false;  // "-- izin:" satırı var
};

static uint8_t luaIconByName(const String& n);
static uint8_t luaParsePerms(const String& v) {
  uint8_t m = 0;
  String w = trLower(v) + ",";
  int a = 0;
  for (int c = w.indexOf(','); c >= 0; a = c + 1, c = w.indexOf(',', a)) {
    String t = w.substring(a, c);
    t.trim();
    for (int i = 0; i < 6; i++)
      if (t == LUA_PERMS[i]) m |= 1 << i;
  }
  return m;
}

static String luaPermText(uint8_t m) {
  String s;
  for (int i = 0; i < 6; i++)
    if (m & (1 << i)) s += (s.length() ? ", " : "") + String(LUA_PERMS[i]);
  return s.length() ? s : String("hiçbiri");
}

// Başlıktaki "-- anahtar: değer" satırı
static void luaParseHeader(LuaAppInfo& a, const String& line) {
  String body = line.substring(2);
  body.trim();
  int c = body.indexOf(':');
  if (c < 0) return;
  String key = body.substring(0, c), v = body.substring(c + 1);
  v.trim();
  if (key == "ad" && v.length()) a.name = v;
  else if (key == "simge") a.icon = luaIconByName(trLower(v));
  else if (key == "arkaplan") a.bg = trLower(v) == "evet";
  else if (key == "surum") a.ver = v.substring(0, 12);
  else if (key == "izin") {
    a.perm = luaParsePerms(v);
    a.manifest = true;
  }
}
static std::vector<LuaAppInfo> luaApps;
static bool luaScanned = false;
static int luaCur = -1;  // luaApps içindeki yer; -2: gizli özellik (luaExtra)
static LuaAppInfo luaExtra;

// Süreç durumu (LS yalnızca Lua sürecine aittir)
static lua_State* LS = nullptr;
static const uint32_t LUA_STACK = 14 * 1024;  // süreç yığını
static size_t luaMem = 0, luaMemMax = 0;
static uint32_t luaCallStart = 0, luaLastStep = 0, luaSlice = 0;
static Proc* luaProcP = nullptr;
static String luaRunPath, luaRunName;  // çalışan sürecin dosyası (liste yeniden okunsa da değişmez)
static bool luaRunBg = false;
static volatile bool luaQuit = false, luaErrPending = false;
static int luaSvcPid = -1;
// Arayüzle ortak (dizgiler mtx kilidiyle)
static String luaErr, luaToastMsg;
static volatile bool luaRawInput = false, luaHasStep = false, luaExitReq = false;
static volatile bool luaDrawReq = false, luaErrNew = false, luaFg = false;
static int luaPid = -1;

// Arayüzden sürece iletiler
enum LuaEvKind : uint8_t { LE_TAP, LE_SWIPE, LE_DOWN, LE_MOVE, LE_UP, LE_CLOSE };
struct LuaEv {
  uint8_t k;
  int16_t x, y;
};
static std::deque<LuaEv> luaQ;

static void luaPost(LuaEv e) {
  {
    Lock l;
    if (e.k == LE_MOVE && !luaQ.empty() && luaQ.back().k == LE_MOVE) luaQ.back() = e;  // sürükleme birleştirilir
    else if (luaQ.size() < 24 || e.k == LE_CLOSE) luaQ.push_back(e);
  }
  kNotify(luaPid);
}

// Sürecin ekrana çizebileceği an: uygulama önde ve üstünde pencere/klavye/afiş yok (Gfx içinde çağrılır)
static bool luaCanDraw() { return luaFg && !shellOverlayActive(); }

// --- Simge adları (-- simge: ...) ---
struct LuaIconName {
  const char* name;
  uint8_t id;
};
static const LuaIconName LUA_ICONS[] = {
    {"kod", I_CODE},       {"oyun", I_GAMEPAD},   {"zar", I_DICE},     {"saat", I_CLOCK},      {"alarm", I_ALARM},
    {"not", I_NOTES},      {"liste", I_LIST},     {"hesap", I_CALC},   {"grafik", I_CHART},    {"muzik", I_NOTE},
    {"kalem", I_PENCIL},   {"kitap", I_BOOK},     {"isik", I_LIGHT},   {"hedef", I_TARGET},    {"kronometre", I_STOPWATCH},
    {"dunya", I_GLOBE},    {"bulut", I_CLOUD},    {"gunes", I_SUN},    {"ates", I_FLAME},      {"bayrak", I_FLAG},
    {"kart", I_CARDS},     {"yilan", I_SNAKE},    {"foto", I_PHOTO},   {"klavye", I_KEYBOARD}, {"cip", I_CHIP},
    {"nabiz", I_PULSE},
};

static uint8_t luaIconByName(const String& n) {
  for (auto& e : LUA_ICONS)
    if (n == e.name) return e.id;
  return I_CODE;
}

static void luaEnsureExamples();

static void luaScan() {
  luaApps.clear();
  luaScanned = true;
  if (!sdOk) return;
  luaEnsureExamples();
  File d = SD.open(LUA_APPS_DIR);
  if (!d) return;
  for (File f = d.openNextFile(); f && luaApps.size() < 40; f = d.openNextFile()) {
    String n = baseName(f.name());
    if (!f.isDirectory() && extOf(n) == "lua") {
      LuaAppInfo a;
      a.path = joinPath(LUA_APPS_DIR, n);
      a.name = n.substring(0, n.lastIndexOf('.'));
      for (int li = 0; li < 10 && f.available(); li++) {  // ilk yorum satırları: ad, simge, sürüm, izin…
        String ln = f.readStringUntil('\n');
        ln.trim();
        if (!ln.startsWith("--")) break;
        luaParseHeader(a, ln);
      }
      luaApps.push_back(a);
    }
    f.close();
  }
  d.close();
  std::sort(luaApps.begin(), luaApps.end(), [](const LuaAppInfo& a, const LuaAppInfo& b) { return trLower(a.name) < trLower(b.name); });
}

static int luaAppCount() {
  if (!luaScanned) luaScan();
  return luaApps.size();
}
static String luaAppLabel(int i) { return i < (int)luaApps.size() ? luaApps[i].name : String(""); }
static uint8_t luaAppIcon(int i) { return i < (int)luaApps.size() ? luaApps[i].icon : I_CODE; }

// ============================================================================
// Çalışma ortamı
// ============================================================================
static void* luaAlloc(void*, void* ptr, size_t osize, size_t nsize) {
  if (!ptr) osize = 0;
  if (nsize == 0) {
    free(ptr);
    luaMem -= osize;
    return nullptr;
  }
  if (luaMem - osize + nsize > luaMemMax) return nullptr;  // bellek sınırı: Lua "not enough memory" verir
  void* p = realloc(ptr, nsize);
  if (p) luaMem = luaMem - osize + nsize;
  return p;
}

// Her 1000 Lua komutunda bir: sonlandırma isteği ve zaman dilimi
static void luaHook(lua_State* L, lua_Debug* ar) {
  if (luaProcP && luaProcP->killReq) luaL_error(L, "sonlandırıldı");
#ifndef HOST
  if (millis() - luaSlice > 40) {  // uzun hesapta düşük öncelikli görevlere de nefes
    vTaskDelay(1);
    luaSlice = millis();
  }
#else
  if (millis() - luaCallStart > 4000) {  // önizlemede sonlandıracak kimse yok
    if (lua_getinfo(L, "Sl", ar) && ar->currentline > 0) luaL_error(L, "%s:%d: zaman aşımı", ar->short_src, ar->currentline);
    luaL_error(L, "zaman aşımı");
  }
#endif
}

static String luaDataDirOf(const String& path) {
  String stem = baseName(path);
  stem = stem.substring(0, stem.lastIndexOf('.'));
  return String(LUA_APPS_DIR "/veri/") + safeName(stem);
}
static String luaDataDir() { return luaDataDirOf(luaRunPath); }

static String luaEnsureDir() {
  String dir = luaDataDir();
  if (!SD.exists(dir)) {
    if (!SD.exists(LUA_APPS_DIR "/veri")) SD.mkdir(LUA_APPS_DIR "/veri");
    SD.mkdir(dir);
  }
  return dir;
}

static String luaSafeFile(lua_State* L, int idx) {
  String n = luaL_checkstring(L, idx);
  if (n.indexOf('/') >= 0 || n.indexOf('\\') >= 0 || n.indexOf("..") >= 0 || !n.length()) luaL_error(L, "geçersiz dosya adı: %s", n.c_str());
  return joinPath(luaEnsureDir(), n);
}

// Uygulamanın günlüğü: veri/<ad>/gunluk.txt; 8 KB'ı geçince gunluk.eski.txt olur (son 8–16 KB kalır)
static void luaLog(const String& m) {
  if (!sdOk || !luaRunPath.length()) return;
  String dir = luaEnsureDir(), p = dir + "/gunluk.txt";
  File f = SD.open(p, FILE_APPEND);
  if (f && f.size() > 8192) {
    f.close();
    String old = dir + "/gunluk.eski.txt";
    SD.remove(old);
    SD.rename(p, old);
    f = SD.open(p, FILE_APPEND);
  }
  if (!f) return;
  char t[24];
  struct tm tm;
  if (localNow(tm)) strftime(t, sizeof t, "%d.%m %H:%M:%S ", &tm);
  else snprintf(t, sizeof t, "%lus ", (unsigned long)(millis() / 1000));
  f.print(t);
  f.println(m);
  f.close();
}

static Font luaFont(int s) {
  switch (s) {
    case 1: return F_XS;
    case 3: return F_MD;
    case 4: return F_LG;
    case 5: return F_XL;
    default: return F_SM;
  }
}

static int lclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --- ekran ---
// Not: Lua hataları longjmp ile döner; bu yüzden bütün argümanlar ekran kilidi alınmadan ÖNCE
// okunur (kilit tutulurken hata olursa kilit bırakılmazdı). Koordinatlar ondalıklı da olabilir.
static int lnum(lua_State* L, int i) { return (int)floorf((float)luaL_checknumber(L, i)); }
static int lopt(lua_State* L, int i, int def) { return lua_isnoneornil(L, i) ? def : lnum(L, i); }
static uint16_t lcol(lua_State* L, int i, uint16_t def) { return lua_isnoneornil(L, i) ? def : (uint16_t)(lnum(L, i) & 0xFFFF); }

static int l_temizle(lua_State* L) {
  uint16_t c = lcol(L, 1, C_BG);
  Gfx g;
  if (luaCanDraw()) tft.fillRect(0, HDR_H, SCR_W, SCR_H - HDR_H, c);
  return 0;
}
static int l_dikdortgen(lua_State* L) {
  int x = lnum(L, 1), y = lnum(L, 2), w = lnum(L, 3), h = lnum(L, 4);
  uint16_t c = lcol(L, 5, C_TEXT);
  bool fill = lua_isnoneornil(L, 6) ? true : lua_toboolean(L, 6);
  if (y < HDR_H) {
    h -= HDR_H - y;
    y = HDR_H;
  }
  if (w <= 0 || h <= 0) return 0;
  Gfx g;
  if (!luaCanDraw()) return 0;
  if (fill) tft.fillRect(x, y, w, h, c);
  else tft.drawRect(x, y, w, h, c);
  return 0;
}
static int l_cizgi(lua_State* L) {
  float x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2), x2 = luaL_checknumber(L, 3), y2 = luaL_checknumber(L, 4);
  uint16_t c = lcol(L, 5, C_TEXT);
  float w = luaL_optnumber(L, 6, 1);
  uint16_t bg = lcol(L, 7, C_BG);
  y1 = std::max<float>(y1, HDR_H);
  y2 = std::max<float>(y2, HDR_H);
  Gfx g;
  if (!luaCanDraw()) return 0;
  if (w <= 1.01f) tft.drawLine(x1, y1, x2, y2, c);
  else tft.drawWideLine(x1, y1, x2, y2, std::min(w, 40.f), c, bg);
  return 0;
}
static int l_daire(lua_State* L) {
  int x = lnum(L, 1), y = lnum(L, 2), r = lclamp(lnum(L, 3), 0, 200);
  uint16_t c = lcol(L, 4, C_TEXT);
  bool fill = lua_isnoneornil(L, 5) ? true : lua_toboolean(L, 5);
  if (y - r < HDR_H) return 0;
  Gfx g;
  if (!luaCanDraw()) return 0;
  if (fill) tft.fillCircle(x, y, r, c);
  else tft.drawCircle(x, y, r, c);
  return 0;
}
static int l_piksel(lua_State* L) {
  int x = lnum(L, 1), y = lnum(L, 2);
  uint16_t c = lcol(L, 3, C_TEXT);
  if (y < HDR_H) return 0;
  Gfx g;
  if (luaCanDraw()) tft.drawPixel(x, y, c);
  return 0;
}
static int l_yazi(lua_State* L) {
  size_t len;
  const char* s = luaL_tolstring(L, 1, &len);
  String str(s);
  lua_pop(L, 1);
  int x = lnum(L, 2), y = lnum(L, 3);
  uint16_t c = lcol(L, 4, C_TEXT);
  Font f = luaFont(lopt(L, 5, 2));
  String al = luaL_optstring(L, 6, "sol");
  uint16_t bg = lcol(L, 7, C_BG);
  uint8_t d = al == "orta" ? TC_DATUM : (al == "sag" ? TR_DATUM : TL_DATUM);
  int w;
  {
    Gfx g;
    if (y >= HDR_H && luaCanDraw()) text(str, x, y, f, c, bg, d);
    w = textW(str, f);
  }
  lua_pushinteger(L, w);
  return 1;
}
static int l_genislik(lua_State* L) {
  String str = luaL_checkstring(L, 1);
  Font f = luaFont(lopt(L, 2, 2));
  int w;
  {
    Gfx g;
    w = textW(str, f);
  }
  lua_pushinteger(L, w);
  return 1;
}
static int l_renk(lua_State* L) {
  lua_pushinteger(L, rgbn(lclamp(lnum(L, 1), 0, 255), lclamp(lnum(L, 2), 0, 255), lclamp(lnum(L, 3), 0, 255)));
  return 1;
}
static int l_simge(lua_State* L) {
  String n = luaL_checkstring(L, 1);
  int x = lnum(L, 2), y = lnum(L, 3), sz = lclamp(lopt(L, 4, 24), 8, 96);
  uint16_t c = lcol(L, 5, C_TEXT), bg = lcol(L, 6, C_BG);
  if (y < HDR_H) return 0;
  Gfx g;
  if (luaCanDraw()) drawIcon(luaIconByName(n), x, y, sz, c, bg);
  return 0;
}

// --- zaman ---
static int l_ms(lua_State* L) {
  lua_pushinteger(L, (lua_Integer)(millis() & 0x7FFFFFFF));
  return 1;
}
static int l_saat(lua_State* L) {
  struct tm t;
  if (!localNow(t)) return 0;
  lua_pushinteger(L, t.tm_hour);
  lua_pushinteger(L, t.tm_min);
  lua_pushinteger(L, t.tm_sec);
  return 3;
}
static int l_tarih(lua_State* L) {
  struct tm t;
  if (!localNow(t)) return 0;
  lua_pushinteger(L, t.tm_year + 1900);
  lua_pushinteger(L, t.tm_mon + 1);
  lua_pushinteger(L, t.tm_mday);
  lua_pushinteger(L, t.tm_wday);
  return 4;
}

// --- dosya (uygulamanın kendi klasöründe) ---
static int l_oku(lua_State* L) {
  File f = SD.open(luaSafeFile(L, 1));
  if (!f || f.size() > 32768) return 0;
  String s = f.readString();
  f.close();
  lua_pushlstring(L, s.c_str(), s.length());
  return 1;
}
static int l_yaz_(lua_State* L, const char* mode) {
  String p = luaSafeFile(L, 1);
  size_t len;
  const char* s = luaL_checklstring(L, 2, &len);
  File f = SD.open(p, mode);
  if (!f) return 0;
  f.write((const uint8_t*)s, len);
  f.close();
  lua_pushboolean(L, 1);
  return 1;
}
static int l_yaz(lua_State* L) { return l_yaz_(L, FILE_WRITE); }
static int l_ekle(lua_State* L) { return l_yaz_(L, FILE_APPEND); }
static int l_var(lua_State* L) {
  lua_pushboolean(L, SD.exists(luaSafeFile(L, 1)));
  return 1;
}
static int l_dsil(lua_State* L) {
  lua_pushboolean(L, SD.remove(luaSafeFile(L, 1)));
  return 1;
}
static int l_liste(lua_State* L) {
  lua_newtable(L);
  File d = SD.open(luaDataDir());
  int i = 1;
  if (d)
    for (File f = d.openNextFile(); f && i <= 200; f = d.openNextFile()) {
      lua_pushstring(L, baseName(f.name()).c_str());
      lua_rawseti(L, -2, i++);
      f.close();
    }
  return 1;
}

// --- ayar: anahtar=değer (uygulamanın klasöründe ayarlar.txt) ---
static int l_ayar_al(lua_State* L) {
  String key = luaL_checkstring(L, 1);
  lua_pushstring(L, "ayarlar.txt");
  String p = luaSafeFile(L, lua_gettop(L));
  lua_pop(L, 1);
  File f = SD.open(p);
  if (f) {
    while (f.available()) {
      String ln = f.readStringUntil('\n');
      int e = ln.indexOf('=');
      if (e > 0 && ln.substring(0, e) == key) {
        f.close();
        lua_pushstring(L, ln.substring(e + 1).c_str());
        return 1;
      }
    }
    f.close();
  }
  lua_pushvalue(L, 2);
  return 1;
}
static int l_ayar_kaydet(lua_State* L) {
  String key = luaL_checkstring(L, 1);
  String val = luaL_tolstring(L, 2, nullptr);
  lua_pop(L, 1);
  if (key.indexOf('=') >= 0 || key.indexOf('\n') >= 0) return luaL_error(L, "geçersiz ayar adı");
  val.replace("\n", " ");
  lua_pushstring(L, "ayarlar.txt");
  String p = luaSafeFile(L, lua_gettop(L));
  lua_pop(L, 1);
  String out;
  File f = SD.open(p);
  if (f) {
    while (f.available()) {
      String ln = f.readStringUntil('\n');
      int e = ln.indexOf('=');
      if (e > 0 && ln.substring(0, e) != key) out += ln + "\n";
    }
    f.close();
  }
  out += key + "=" + val + "\n";
  f = SD.open(p, FILE_WRITE);
  if (f) {
    f.print(out);
    f.close();
  }
  return 0;
}

// --- sistem ---
static int l_bildir(lua_State* L) {
  String m = luaL_checkstring(L, 1);
  Lock l;
  luaToastMsg = m;  // balonu arayüz çizer
  return 0;
}
static int l_cikis(lua_State*) {
  luaQuit = true;
  luaExitReq = true;
  return 0;
}
static int l_isik(lua_State* L) {
  lua_pushinteger(L, sysLightPct());
  return 1;
}
static int l_led(lua_State* L) {
  sysLed(lclamp(lnum(L, 1), 0, 255), lclamp(lnum(L, 2), 0, 255), lclamp(lnum(L, 3), 0, 255));
  return 0;
}
static int l_bellek(lua_State* L) {
  lua_pushinteger(L, (lua_Integer)(luaMemMax - luaMem));
  return 1;
}
static int l_yaz_seri(lua_State* L) {  // print → seri port
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    Serial.print(luaL_tolstring(L, i, nullptr));
    lua_pop(L, 1);
    if (i < n) Serial.print('\t');
  }
  Serial.println();
  return 0;
}

static void luaSetField(lua_State* L, const char* k, lua_Integer v) {
  lua_pushinteger(L, v);
  lua_setfield(L, -2, k);
}
static void luaSetStr(lua_State* L, const char* k, const char* v) {
  lua_pushstring(L, v);
  lua_setfield(L, -2, k);
}

// Salt okunur sistem bilgisi (gerçek değerler)
static int l_bilgi(lua_State* L) {
  lua_createtable(L, 0, 5);
  luaSetField(L, "surum", FW_BUILD);
  luaSetStr(L, "cip", sysChipText().c_str());
  luaSetField(L, "bos", sysHeapFree());
  luaSetField(L, "enaz", sysHeapMin());
  luaSetField(L, "calisma", millis() / 1000);
  return 1;
}

static int l_gunluk(lua_State* L) {
  luaLog(String("uygulama: ") + luaL_checkstring(L, 1));
  return 0;
}

static int l_noperm(lua_State* L) { return luaL_error(L, "izin yok: %s", lua_tostring(L, lua_upvalueindex(1))); }

// load yalnız metin (derlenmiş kod VM'i bozabilir); dizgi parçası, ad, kip (yok sayılır), ortam
static int l_load(lua_State* L) {
  size_t n;
  const char* src = luaL_checklstring(L, 1, &n);
  const char* name = luaL_optstring(L, 2, "=(load)");
  int env = lua_isnone(L, 4) ? 0 : 4;
  if (luaL_loadbufferx(L, src, n, name, "t") == LUA_OK) {
    if (env) {
      lua_pushvalue(L, env);
      if (!lua_setupvalue(L, -2, 1)) lua_pop(L, 1);
    }
    return 1;
  }
  luaL_pushfail(L);
  lua_insert(L, -2);
  return 2;
}

// --- wifi ve bluetooth (yalnız okuma) ---
static bool luaBtUsed = false;
static int l_wifi_durum(lua_State* L) {
  bool c = wifiConnected();
  lua_createtable(L, 0, 4);
  lua_pushboolean(L, c);
  lua_setfield(L, -2, "bagli");
  if (c) {
    luaSetStr(L, "ag", wifiSsid().c_str());
    luaSetField(L, "sinyal", wifiRssi());
    luaSetStr(L, "ip", wifiIp().c_str());
  }
  return 1;
}
static int l_bt_tara(lua_State* L) {
  int sn = lclamp(lopt(L, 1, 10), 3, 30);
  luaBtUsed = true;
  if (bsBusy() || bsStart(sn * 1000UL, false)) {
    lua_pushboolean(L, 1);
    return 1;
  }
  luaL_pushfail(L);
  lua_pushstring(L, bsError().c_str());
  return 2;
}
static int l_bt_suruyor(lua_State* L) {
  lua_pushboolean(L, bsBusy());
  return 1;
}
static int l_bt_cihazlar(lua_State* L) {
  luaBtUsed = true;
  int n = bsCount();
  lua_createtable(L, n, 0);
  BleDev d[4];
  for (int i = 0; i < n; i += 4) {
    int got = bsGet(i, d, 4);
    for (int k = 0; k < got; k++) {
      lua_createtable(L, 0, 5);
      luaSetStr(L, "ad", d[k].name);
      luaSetStr(L, "adres", macText(d[k].addr).c_str());
      luaSetField(L, "sinyal", d[k].rssi);
      luaSetField(L, "enguclu", d[k].rssiMax);
      const char* c = bleCompany(d[k].mfr);
      luaSetStr(L, "uretici", c ? c : "");
      lua_rawseti(L, -2, i + k + 1);
    }
  }
  return 1;
}

// Kütüphane tablosu: izni olmayan işlevin yerine "izin yok: …" hatası veren işlev konur
static uint8_t luaRunPerm = LP_ESKI;
struct LuaFn {
  const char* n;
  lua_CFunction f;
  uint8_t p;  // gereken izin (0: herkese açık)
};
static void luaLib(lua_State* L, const LuaFn* fs, size_t n, const char* global) {
  lua_createtable(L, 0, n);
  for (size_t i = 0; i < n; i++) {
    uint8_t p = fs[i].p;
    if (!p || (luaRunPerm & p)) {
      lua_pushcfunction(L, fs[i].f);
    } else {
      int b = 0;
      while (!((p >> b) & 1)) b++;
      lua_pushstring(L, LUA_PERMS[b]);
      lua_pushcclosure(L, l_noperm, 1);
    }
    lua_setfield(L, -2, fs[i].n);
  }
  if (global) lua_setglobal(L, global);
}
#define LUA_LIB(L, a, g) luaLib(L, a, sizeof(a) / sizeof(a[0]), g)

static void luaOpenApi(lua_State* L) {
  static const LuaFn ekran[] = {{"temizle", l_temizle, LP_EKRAN}, {"dikdortgen", l_dikdortgen, LP_EKRAN}, {"cizgi", l_cizgi, LP_EKRAN},
                                {"daire", l_daire, LP_EKRAN},     {"piksel", l_piksel, LP_EKRAN},         {"yazi", l_yazi, LP_EKRAN},
                                {"genislik", l_genislik, 0},      {"renk", l_renk, 0},                    {"simge", l_simge, LP_EKRAN}};
  LUA_LIB(L, ekran, nullptr);
  luaSetField(L, "en", SCR_W);
  luaSetField(L, "boy", SCR_H);
  luaSetField(L, "ust", HDR_H);
  lua_setglobal(L, "ekran");

  lua_newtable(L);
  luaSetField(L, "zemin", C_BG);
  luaSetField(L, "yuzey", C_SURF);
  luaSetField(L, "yuzey2", C_SURF2);
  luaSetField(L, "gumus", C_TEXT);
  luaSetField(L, "gri", C_TEXT2);
  luaSetField(L, "soluk", C_TEXT3);
  luaSetField(L, "cizgi", C_LINE);
  luaSetField(L, "vurgu", C_ACC);
  luaSetField(L, "siyah", 0);
  luaSetField(L, "beyaz", 0xFFFF);
  luaSetField(L, "kirmizi", rgbn(214, 92, 80));
  luaSetField(L, "turuncu", rgbn(226, 150, 72));
  luaSetField(L, "sari", rgbn(222, 200, 90));
  luaSetField(L, "yesil", rgbn(110, 176, 110));
  luaSetField(L, "mavi", rgbn(90, 120, 210));
  luaSetField(L, "mor", rgbn(160, 110, 200));
  lua_setglobal(L, "renk");

  static const LuaFn zaman[] = {{"ms", l_ms, 0}, {"saat", l_saat, 0}, {"tarih", l_tarih, 0}};
  LUA_LIB(L, zaman, "zaman");
  static const LuaFn dosya[] = {{"oku", l_oku, LP_DOSYA}, {"yaz", l_yaz, LP_DOSYA}, {"ekle", l_ekle, LP_DOSYA},
                                {"var", l_var, LP_DOSYA}, {"sil", l_dsil, LP_DOSYA}, {"liste", l_liste, LP_DOSYA}};
  LUA_LIB(L, dosya, "dosya");
  static const LuaFn ayar[] = {{"al", l_ayar_al, LP_DOSYA}, {"kaydet", l_ayar_kaydet, LP_DOSYA}};
  LUA_LIB(L, ayar, "ayar");
  static const LuaFn sistem[] = {{"bildir", l_bildir, LP_BILDIRIM}, {"cikis", l_cikis, 0}, {"isik", l_isik, 0},
                                 {"led", l_led, LP_LED},            {"bellek", l_bellek, 0}, {"gunluk", l_gunluk, 0},
                                 {"bilgi", l_bilgi, 0}};
  LUA_LIB(L, sistem, "sistem");
  static const LuaFn wifi[] = {{"durum", l_wifi_durum, LP_WIFI}};
  LUA_LIB(L, wifi, "wifi");
  static const LuaFn bt[] = {{"tara", l_bt_tara, LP_BT}, {"suruyor", l_bt_suruyor, LP_BT}, {"cihazlar", l_bt_cihazlar, LP_BT}};
  LUA_LIB(L, bt, "bluetooth");

  lua_pushcfunction(L, l_yaz_seri);
  lua_setglobal(L, "print");
  lua_pushcfunction(L, l_load);
  lua_setglobal(L, "load");
  // dosya sistemine doğrudan erişen temel işlevler kapatılır
  lua_pushnil(L);
  lua_setglobal(L, "dofile");
  lua_pushnil(L);
  lua_setglobal(L, "loadfile");
}

static bool luaHas(const char* fn) {
  if (!LS) return false;
  lua_getglobal(LS, fn);
  bool f = lua_isfunction(LS, -1);
  lua_pop(LS, 1);
  return f;
}

static void luaShutdown() {
  if (LS) lua_close(LS);
  LS = nullptr;
  luaMem = 0;
}

static void luaSetError(const String& e) {
  String m = e;
  m.replace(LUA_APPS_DIR "/", "");
  luaLog("hata: " + m);
  {
    Lock l;
    luaErr = m;
  }
  if (!luaFg) luaErrPending = true;  // arka plandayken: uygulama açılınca gösterilir
  luaErrNew = true;
}

// --- Süreç tarafı ---
// İşlevi çağırır (tanımlı değilse false). Hata olursa Lua kapanır, hata arayüze bildirilir.
static bool luaCallFn(const char* fn, std::initializer_list<lua_Integer> ints = {}, const char* str = nullptr) {
  if (!LS) return false;
  lua_getglobal(LS, fn);
  if (!lua_isfunction(LS, -1)) {
    lua_pop(LS, 1);
    return false;
  }
  int n = 0;
  if (str) {
    lua_pushstring(LS, str);
    n++;
  }
  for (auto v : ints) {
    lua_pushinteger(LS, v);
    n++;
  }
  luaCallStart = luaSlice = millis();
  uint32_t t0 = micros();
  int r = lua_pcall(LS, n, 0, 0);
  kAddBusy(luaProcP, micros() - t0);
  if (r != LUA_OK) {
    bool killed = luaProcP && luaProcP->killReq;
    if (!killed) luaSetError(lua_tostring(LS, -1) ? lua_tostring(LS, -1) : "bilinmeyen hata");
    luaShutdown();
  }
  return true;
}

static void luaProcInit() {
  String path = luaRunPath;
  File f = SD.open(path);
  if (!f) {
    luaSetError("Dosya açılamadı");
    return;
  }
  String src = f.readString();
  f.close();
  // Bellek: Bluetooth ve Wi-Fi açıkken boş yığının bir kısmı (sürecin yığını ayrıldıktan sonra)
  luaMemMax = std::min<size_t>(96 * 1024, sysHeapMaxAlloc() > 24 * 1024 ? sysHeapFree() - 24 * 1024 : 16 * 1024);
  luaMemMax = std::max<size_t>(luaMemMax, 16 * 1024);
  luaMem = 0;
  LS = lua_newstate(luaAlloc, nullptr);
  if (!LS) {
    luaSetError("Bellek yetmedi");
    return;
  }
  lua_gc(LS, LUA_GCGEN, 0, 0);  // kuşak tabanlı çöp toplama: kısa ömürlü nesnelerde kısa duraklamalar
  luaL_requiref(LS, "_G", luaopen_base, 1);
  luaL_requiref(LS, LUA_TABLIBNAME, luaopen_table, 1);
  luaL_requiref(LS, LUA_STRLIBNAME, luaopen_string, 1);
  luaL_requiref(LS, LUA_MATHLIBNAME, luaopen_math, 1);
  luaL_requiref(LS, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  luaL_requiref(LS, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_settop(LS, 0);
  luaOpenApi(LS);
  lua_sethook(LS, luaHook, LUA_MASKCOUNT, 1000);
  String chunk = "@" + baseName(path);
  luaCallStart = luaSlice = millis();
  uint32_t t0 = micros();
  int r = luaL_loadbufferx(LS, src.c_str(), src.length(), chunk.c_str(), "t");
  src = String();
  if (r == LUA_OK) r = lua_pcall(LS, 0, 0, 0);
  kAddBusy(luaProcP, micros() - t0);
  if (r != LUA_OK) {
    if (!(luaProcP && luaProcP->killReq)) luaSetError(lua_tostring(LS, -1) ? lua_tostring(LS, -1) : "yüklenemedi");
    luaShutdown();
    return;
  }
  luaRawInput = luaHas("bas") || luaHas("surukle") || luaHas("birak");
  luaHasStep = luaHas("adim");
  luaLastStep = millis();
  luaCallFn("baslat");
  luaDrawReq = true;
}

static void luaDispatch(const LuaEv& e) {
  static const char* const DIRS[] = {"sol", "sag", "yukari", "asagi"};
  switch (e.k) {
    case LE_TAP: luaCallFn("dokun", {e.x, e.y}); break;
    case LE_SWIPE: luaCallFn("kaydir", {}, DIRS[e.x & 3]); break;
    case LE_DOWN: luaCallFn("bas", {e.x, e.y}); break;
    case LE_MOVE: luaCallFn("surukle", {e.x, e.y}); break;
    case LE_UP: luaCallFn("birak", {e.x, e.y}); break;
  }
}

// Bir tur: iletileri işle, gerekirse çiz, adım at. false: süreç bitmeli.
static bool luaProcStep(bool block) {
#ifndef HOST
  if (block) {
    uint32_t wait = 250;
    if (luaHasStep) {
      uint32_t el = millis() - luaLastStep;
      wait = el >= 33 ? 0 : 33 - el;
    }
    if (wait) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
  }
#endif
  for (int n = 0; n < 16 && LS; n++) {
    LuaEv e;
    {
      Lock l;
      if (luaQ.empty()) break;
      e = luaQ.front();
      luaQ.pop_front();
    }
    if (e.k == LE_CLOSE) {
      luaCallFn("kapat");
      luaShutdown();
      return false;
    }
    luaDispatch(e);
  }
  if (LS && luaDrawReq) {
    luaDrawReq = false;
    luaCallFn("ciz");
  }
  if (LS && luaHasStep && millis() - luaLastStep >= 33) {
    uint32_t dt = millis() - luaLastStep;
    luaLastStep = millis();
    luaCallFn("adim", {(lua_Integer)std::min<uint32_t>(dt, 1000)});
  }
  if (LS && luaQuit && !luaFg) {  // arka planda sistem.cikis()
    luaCallFn("kapat");
    luaShutdown();
  }
  return LS != nullptr;
}

#ifndef HOST
static void luaProcMain(Proc* p) {
  luaProcP = p;
  luaProcInit();
  while (LS && !p->killReq && luaProcStep(true)) {
  }
  luaShutdown();
  luaProcP = nullptr;
}
#endif

// ============================================================================
// Uygulama (arayüz tarafı)
// ============================================================================
static void luaShowError() {
  String e;
  {
    Lock l;
    e = luaErr;
  }
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  drawIcon(I_CODE, 12, HDR_H + 12, 24, C_ACC, C_BG);
  text("Uygulama hatası", 46, HDR_H + 14, F_MD, C_TEXT, C_BG);
  auto ls = wrapText(e, F_XS, 296, 9);
  for (size_t i = 0; i < ls.size(); i++) text(ls[i], 12, HDR_H + 52 + i * 16, F_XS, C_TEXT2, C_BG);
  text("Dosyayı düzelt, uygulamayı yeniden aç.", 12, SCR_H - 18, F_XS, C_TEXT3, C_BG);
}

static bool luaHasError() {
  Lock l;
  return luaErr.length() > 0;
}

static void luaDraw(bool full) {
  if (full) {
    header(luaRunName.length() ? luaRunName : String("Uygulama"));
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  if (luaHasError()) {
    luaShowError();
    return;
  }
  luaDrawReq = true;
  kNotify(luaPid);
}

static void luaTap(int x, int y) {
  if (y >= HDR_H) luaPost({LE_TAP, (int16_t)x, (int16_t)y});
}

static void luaSwipe(int dir) { luaPost({LE_SWIPE, (int16_t)dir, 0}); }

static bool luaRaw(uint8_t ev, int x, int y) {
  if (!luaRawInput || luaHasError()) return false;
  if (ev == TE_DOWN && y < HDR_H) return false;
  luaPost({(uint8_t)(ev == TE_DOWN ? LE_DOWN : (ev == TE_MOVE ? LE_MOVE : LE_UP)), (int16_t)x, (int16_t)y});
  return true;
}

// Çekirdek servisi (50 ms): süreçten gelen balon ve hatalar; uygulama arka plandayken de çalışır
static void luaSvc() {
  String t;
  {
    Lock l;
    t = luaToastMsg;
    luaToastMsg = "";
  }
  if (t.length()) uiToast(t);
  if (luaErrNew && !luaFg) {
    luaErrNew = false;
    uiToast(luaRunName + " durdu: hata");
  }
}

static void luaTick() {
  if (luaExitReq) {
    luaExitReq = false;
    appExit();
    return;
  }
  if (luaErrNew) {
    luaErrNew = false;
    if (luaCanDraw()) luaShowError();
  }
#ifdef HOST
  luaSvc();
  if (LS) luaProcStep(false);  // önizlemede süreç yok: arayüz turunda işlenir
#endif
}

static const LuaAppInfo* luaCurInfo() {
  if (luaCur == -2) return &luaExtra;
  return luaCur >= 0 && luaCur < (int)luaApps.size() ? &luaApps[luaCur] : nullptr;
}

static void luaOpenFn() {
  const LuaAppInfo* ci = luaCurInfo();
  String path = ci ? ci->path : String("");
  if (kAlive(luaPid) && path == luaRunPath) {  // arka plandaki süreç öne alınır
    luaFg = true;
    luaExitReq = false;
    return;
  }
  if (luaErrPending && path == luaRunPath) {  // arka planda hatayla durmuştu: önce hata gösterilir
    luaErrPending = luaErrNew = false;
    luaFg = true;
    return;
  }
#ifndef HOST
  if (luaSvcPid < 0) luaSvcPid = kService("lua ileti", 50, luaSvc);
#endif
  {
    Lock l;
    luaErr = "";
    luaToastMsg = "";
    luaQ.clear();
  }
  luaRunPath = path;
  luaRunName = ci ? ci->name : String("Uygulama");
  luaRunBg = ci && ci->bg;
  luaRunPerm = ci ? ci->perm : LP_ESKI;
  luaBtUsed = false;
  luaErrNew = luaDrawReq = luaExitReq = luaRawInput = luaHasStep = luaQuit = luaErrPending = false;
  luaFg = true;
  // Süreç yığını + en az 16 KB Lua belleği (os/membudget.h; fazlası boş belleğe göre verilir)
  String why;
  if (!memAsk(MJ_LUA, LUA_STACK + 16 * 1024, LUA_STACK, &why)) {
    luaSetError(why);
    return;
  }
#ifdef HOST
  luaProcP = nullptr;
  luaProcInit();
#else
  String stem = baseName(path);
  stem = stem.substring(0, stem.lastIndexOf('.'));
  String n = "lua:" + stem;
  luaPid = kSpawn(n.c_str(), luaProcMain, LUA_STACK, true);
  if (luaPid < 0) luaSetError("Süreç başlatılamadı: bellek yetmedi");
#endif
}

// Lua sürecini durdurur: önce kapat() için ileti, olmazsa sonlandırma
static bool luaStopProc() {
#ifdef HOST
  luaCallFn("kapat");
  luaShutdown();
  return true;
#else
  if (!kAlive(luaPid)) return true;
  luaPost({LE_CLOSE, 0, 0});
  if (kWait(luaPid, 400)) return true;
  kKill(luaPid);
  return kWait(luaPid, 1500);
#endif
}

static void luaCloseFn() {
  bool keep = luaRunBg && !luaQuit && kAlive(luaPid) && !luaHasError();
  luaFg = false;
  if (keep) return;  // arka planda sürer; Görevler'den sonlandırılabilir
  luaStopProc();
  if (luaBtUsed) bsRelease(false);  // uygulamanın başlattığı Bluetooth taraması ve listesi
  luaBtUsed = false;
}

static const App APP_LUA = {"Uygulama", "Uygulama", I_CODE, luaOpenFn, luaDraw, luaTap, nullptr, luaSwipe, luaRaw, luaTick, nullptr, luaCloseFn};

// --- İzin onayı: NVS'de (luaizin) dosya yolunun özetiyle; değer = dosyanın CRC32'si ve izinler.
// Dosya ya da izin satırı değişince yeniden sorulur.
static uint32_t luaFileCrc(const String& path) {
  File f = SD.open(path);
  if (!f) return 0;
  uint8_t b[256];
  uint32_t c = 0;
  for (int n; (n = f.read(b, sizeof b)) > 0;) c = crc32Upd(c, b, n);
  f.close();
  return c;
}
static String luaPermKey(const String& path) {
  char k[12];
  snprintf(k, sizeof k, "a%08x", (unsigned)crc32Upd(0, (const uint8_t*)path.c_str(), path.length()));
  return k;
}
static uint64_t luaApproval(const String& path) {
  Lock l;
  prefs.begin("luaizin", true);
  uint64_t v = prefs.getULong64(luaPermKey(path).c_str(), 0);
  prefs.end();
  return v;
}
static void luaSetApproval(const String& path, uint64_t v) {
  Lock l;
  prefs.begin("luaizin", false);
  if (v) prefs.putULong64(luaPermKey(path).c_str(), v);
  else prefs.remove(luaPermKey(path).c_str());
  prefs.end();
}
static uint64_t luaWant(const LuaAppInfo& a) { return (uint64_t)luaFileCrc(a.path) << 8 | a.perm; }

static void luaAppOpenNow(int i) {
  if (i < 0 || i >= (int)luaApps.size()) return;
  if (kAlive(luaPid) && luaApps[i].path != luaRunPath && !luaStopProc()) {  // arka plandaki başka uygulama
    toast("Önceki uygulama kapanıyor, biraz bekle");
    return;
  }
  luaCur = i;
  appOpen(&APP_LUA);
}

static void luaAppOpen(int i) {
  if (i < 0 || i >= (int)luaApps.size()) return;
  const LuaAppInfo& a = luaApps[i];
  uint64_t want = luaWant(a), have = luaApproval(a.path);
  if (have == want) {
    luaAppOpenNow(i);
    return;
  }
  String path = a.path;
  String msg = String(have ? "Uygulama değişmiş. " : "") + "Kullanacakları: " + luaPermText(a.perm) +
               (a.manifest ? "" : " (izin satırı yok, eski uygulama)") + ". İzin verilsin mi?";
  dialog(a.name, msg, {"Vazgeç", "İzin ver"}, [i, path, want](int c) {
    if (c != 1) return;
    luaSetApproval(path, want);
    sysLog("lua", "izin verildi: " + baseName(path));
    luaAppOpenNow(i);
  }, 1);
}

// --- Gizli özellikler (docs/gizli.md): ekranları SD varlık paketindeki Lua (ui/font.h);
// tetiklenince /daxen/gizli/'ye açılıp yerleşik uygulama olarak çalışır (izin sorulmaz, izinleri
// dosyanın kendi satırında). Hiçbiri arka planda çalışmaz. SD kart gerekir.
#define LUA_GIZLI_DIR "/daxen/gizli"
static void gizliAc(const char* file) {
  String src = String("gizli/") + file, path = joinPath(LUA_GIZLI_DIR, file);
  uint32_t off, len;
  if (!sdOk || !packFind(src.c_str(), off, len)) {
    toast(sdOk ? "Varlık paketi henüz inmedi" : "Bunun için SD kart gerekiyor");
    return;
  }
  File f = SD.open(path);
  bool ok = f && f.size() == len;
  if (f) f.close();
  if (!ok) {
    if (!SD.exists("/daxen")) SD.mkdir("/daxen");
    if (!SD.exists(LUA_GIZLI_DIR)) SD.mkdir(LUA_GIZLI_DIR);
    if (!packCopy(src.c_str(), path)) {
      toast("Yazılamadı");
      return;
    }
  }
  if (kAlive(luaPid) && luaRunPath != path && !luaStopProc()) {
    toast("Önceki uygulama kapanıyor, biraz bekle");
    return;
  }
  luaExtra = LuaAppInfo();
  luaExtra.path = path;
  luaExtra.name = file;
  File h = SD.open(path);
  for (int li = 0; h && li < 10 && h.available(); li++) {
    String ln = h.readStringUntil('\n');
    ln.trim();
    if (!ln.startsWith("--")) break;
    luaParseHeader(luaExtra, ln);
  }
  if (h) h.close();
  luaExtra.bg = false;
  luaCur = -2;
  sysLog("gizli", luaExtra.name);
  appOpen(&APP_LUA);
}

// Developer Awakening: geliştirici seviyesi ilk kez açılınca bir kez
static void gizliDevFirst() {
  bool seen;
  {
    Lock l;
    prefs.begin("cs", false);
    seen = prefs.getBool("gzdev", false);
    if (!seen) prefs.putBool("gzdev", true);
    prefs.end();
  }
  if (!seen) gizliAc("uyanis.lua");
}

// --- Yönetim: kaldırma (onaylı; veri klasörü kalır) ve Dosyalar'dan yükleme ---
static void luaRemoveAsk(int i) {
  if (i < 0 || i >= (int)luaApps.size()) return;
  String path = luaApps[i].path, name = luaApps[i].name;
  dialog("Kaldırılsın mı?", name + " silinecek. Uygulamanın verileri (veri klasörü) kalır.", {"Vazgeç", "Kaldır"}, [path, name](int c) {
    if (c != 1) return;
    if (kAlive(luaPid) && luaRunPath == path && !luaStopProc()) {
      toast("Uygulama kapanmadı, sonra dene");
      return;
    }
    if (!SD.remove(path)) {
      toast("Silinemedi");
      return;
    }
    luaSetApproval(path, 0);
    luaScanned = false;
    sysLog("lua", "kaldırıldı: " + baseName(path));
    toast(name + " kaldırıldı");
    shellRedraw(true);
  }, 1);
}

static void luaInstall(const String& from) {
  luaAppCount();  // klasör yoksa oluşur
  String to = joinPath(LUA_APPS_DIR, baseName(from));
  auto go = [from, to]() {
    if (!fCopyFile(from, to)) {
      toast("Yüklenemedi");
      return;
    }
    luaScanned = false;
    sysLog("lua", "yüklendi: " + baseName(to));
    toast("Yüklendi; Uygulamalar'da, ilk açılışta izin sorulur");
  };
  if (!SD.exists(to)) go();
  else dialog("Üzerine yazılsın mı?", baseName(to) + " zaten yüklü. Yeni dosyayla değiştirilsin mi?", {"Vazgeç", "Değiştir"}, [go](int c) {
    if (c == 1) go();
  }, 1);
}

// ============================================================================
// Örnek uygulamalar ve rehber: SD'deki varlık paketinden (ui/font.h, kaynakları firmware/lua/),
// klasör yoksa bir kez kopyalanır. Rehber yeni sürümde bir kez yenilenir.
// ============================================================================
static void luaWriteFile(const char* path, const char* data) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  f.write((const uint8_t*)data, strlen(data));
  f.close();
}

static const char* const LUA_EXAMPLES[] = {"merhaba.lua", "zar.lua", "sayac.lua", "top.lua", "zamanlayici.lua", "REHBER.txt"};

static void luaEnsureExamples() {
  if (!packOk) return;  // paket inince yapılır
  bool fresh = !SD.exists(LUA_APPS_DIR);
  if (fresh) {
    if (!SD.exists("/daxen")) SD.mkdir("/daxen");
    SD.mkdir(LUA_APPS_DIR);
  }
  if (!fresh && SD.exists(LUA_APPS_DIR "/veri/.surum3")) return;
  // ilk kez: bütün örnekler; güncellemede yalnız rehber (silinen örnekler geri gelmesin)
  for (const char* n : LUA_EXAMPLES)
    if (fresh || !strcmp(n, "REHBER.txt")) packCopy((String("lua/") + n).c_str(), joinPath(LUA_APPS_DIR, n));
  if (!SD.exists(LUA_APPS_DIR "/veri")) SD.mkdir(LUA_APPS_DIR "/veri");
  luaWriteFile(LUA_APPS_DIR "/veri/.surum3", "3");
}
