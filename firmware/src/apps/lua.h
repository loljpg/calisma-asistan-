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

struct LuaAppInfo {
  String path, name;
  uint8_t icon = I_CODE;
  bool bg = false;  // "-- arkaplan: evet": çıkınca kapanmaz, arka planda sürer
};
static std::vector<LuaAppInfo> luaApps;
static bool luaScanned = false;
static int luaCur = -1;

// Süreç durumu (LS yalnızca Lua sürecine aittir)
static lua_State* LS = nullptr;
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
      for (int li = 0; li < 6 && f.available(); li++) {  // ilk satırlardaki "-- ad:" ve "-- simge:"
        String ln = f.readStringUntil('\n');
        ln.trim();
        if (!ln.startsWith("--")) break;
        String body = ln.substring(2);
        body.trim();
        int c = body.indexOf(':');
        if (c < 0) continue;
        String key = body.substring(0, c), v = body.substring(c + 1);
        v.trim();
        if (key == "ad" && v.length()) a.name = v;
        else if (key == "simge") a.icon = luaIconByName(trLower(v));
        else if (key == "arkaplan") a.bg = trLower(v) == "evet";
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

static String luaDataDir() {
  String stem = baseName(luaRunPath);
  stem = stem.substring(0, stem.lastIndexOf('.'));
  return String(LUA_APPS_DIR "/veri/") + safeName(stem);
}

static String luaSafeFile(lua_State* L, int idx) {
  String n = luaL_checkstring(L, idx);
  if (n.indexOf('/') >= 0 || n.indexOf('\\') >= 0 || n.indexOf("..") >= 0 || !n.length()) luaL_error(L, "geçersiz dosya adı: %s", n.c_str());
  String dir = luaDataDir();
  if (!SD.exists(dir)) {
    if (!SD.exists(LUA_APPS_DIR "/veri")) SD.mkdir(LUA_APPS_DIR "/veri");
    SD.mkdir(dir);
  }
  return joinPath(dir, n);
}

static const uint8_t* luaFont(int s) {
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
static int l_temizle(lua_State* L) {
  Gfx g;
  if (!luaCanDraw()) return 0;
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  if (lua_gettop(L) >= 1) tft.fillRect(0, HDR_H, SCR_W, SCR_H - HDR_H, (uint16_t)luaL_checkinteger(L, 1));
  return 0;
}
static int l_dikdortgen(lua_State* L) {
  int x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2), w = luaL_checkinteger(L, 3), h = luaL_checkinteger(L, 4);
  uint16_t c = (uint16_t)luaL_optinteger(L, 5, C_TEXT);
  bool fill = lua_isnoneornil(L, 6) ? true : lua_toboolean(L, 6);
  Gfx g;
  if (!luaCanDraw()) return 0;
  if (y < HDR_H) {
    h -= HDR_H - y;
    y = HDR_H;
  }
  if (w <= 0 || h <= 0) return 0;
  if (fill) tft.fillRect(x, y, w, h, c);
  else tft.drawRect(x, y, w, h, c);
  return 0;
}
static int l_cizgi(lua_State* L) {
  float x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2), x2 = luaL_checknumber(L, 3), y2 = luaL_checknumber(L, 4);
  uint16_t c = (uint16_t)luaL_optinteger(L, 5, C_TEXT);
  float w = luaL_optnumber(L, 6, 1);
  Gfx g;
  if (!luaCanDraw()) return 0;
  y1 = std::max<float>(y1, HDR_H);
  y2 = std::max<float>(y2, HDR_H);
  if (w <= 1.01f) tft.drawLine(x1, y1, x2, y2, c);
  else tft.drawWideLine(x1, y1, x2, y2, w, c, (uint16_t)luaL_optinteger(L, 7, C_BG));
  return 0;
}
static int l_daire(lua_State* L) {
  int x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2), r = lclamp(luaL_checkinteger(L, 3), 0, 200);
  uint16_t c = (uint16_t)luaL_optinteger(L, 4, C_TEXT);
  bool fill = lua_isnoneornil(L, 5) ? true : lua_toboolean(L, 5);
  if (y - r < HDR_H) return 0;
  Gfx g;
  if (!luaCanDraw()) return 0;
  if (fill) tft.fillCircle(x, y, r, c);
  else tft.drawCircle(x, y, r, c);
  return 0;
}
static int l_piksel(lua_State* L) {
  int x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2);
  Gfx g;
  if (y >= HDR_H && luaCanDraw()) tft.drawPixel(x, y, (uint16_t)luaL_optinteger(L, 3, C_TEXT));
  return 0;
}
static int l_yazi(lua_State* L) {
  size_t len;
  const char* s = luaL_tolstring(L, 1, &len);
  String str(s);
  lua_pop(L, 1);
  int x = luaL_checkinteger(L, 2), y = luaL_checkinteger(L, 3);
  uint16_t c = (uint16_t)luaL_optinteger(L, 4, C_TEXT);
  const uint8_t* f = luaFont(luaL_optinteger(L, 5, 2));
  String al = luaL_optstring(L, 6, "sol");
  uint16_t bg = (uint16_t)luaL_optinteger(L, 7, C_BG);
  uint8_t d = al == "orta" ? TC_DATUM : (al == "sag" ? TR_DATUM : TL_DATUM);
  Gfx g;
  if (y >= HDR_H && luaCanDraw()) text(str, x, y, f, c, bg, d);
  lua_pushinteger(L, textW(str, f));
  return 1;
}
static int l_genislik(lua_State* L) {
  Gfx g;
  lua_pushinteger(L, textW(luaL_checkstring(L, 1), luaFont(luaL_optinteger(L, 2, 2))));
  return 1;
}
static int l_renk(lua_State* L) {
  lua_pushinteger(L, rgbn(lclamp(luaL_checkinteger(L, 1), 0, 255), lclamp(luaL_checkinteger(L, 2), 0, 255), lclamp(luaL_checkinteger(L, 3), 0, 255)));
  return 1;
}
static int l_simge(lua_State* L) {
  String n = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2), y = luaL_checkinteger(L, 3), s = lclamp(luaL_optinteger(L, 4, 24), 8, 96);
  Gfx g;
  if (y >= HDR_H && luaCanDraw()) drawIcon(luaIconByName(n), x, y, s, (uint16_t)luaL_optinteger(L, 5, C_TEXT), (uint16_t)luaL_optinteger(L, 6, C_BG));
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
  sysLed(lclamp(luaL_checkinteger(L, 1), 0, 255), lclamp(luaL_checkinteger(L, 2), 0, 255), lclamp(luaL_checkinteger(L, 3), 0, 255));
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

static void luaOpenApi(lua_State* L) {
  const luaL_Reg ekran[] = {{"temizle", l_temizle}, {"dikdortgen", l_dikdortgen}, {"cizgi", l_cizgi}, {"daire", l_daire},
                            {"piksel", l_piksel},   {"yazi", l_yazi},             {"genislik", l_genislik}, {"renk", l_renk},
                            {"simge", l_simge},     {nullptr, nullptr}};
  luaL_newlib(L, ekran);
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

  const luaL_Reg zaman[] = {{"ms", l_ms}, {"saat", l_saat}, {"tarih", l_tarih}, {nullptr, nullptr}};
  luaL_newlib(L, zaman);
  lua_setglobal(L, "zaman");

  const luaL_Reg dosya[] = {{"oku", l_oku}, {"yaz", l_yaz}, {"ekle", l_ekle}, {"var", l_var}, {"sil", l_dsil}, {"liste", l_liste}, {nullptr, nullptr}};
  luaL_newlib(L, dosya);
  lua_setglobal(L, "dosya");

  const luaL_Reg ayar[] = {{"al", l_ayar_al}, {"kaydet", l_ayar_kaydet}, {nullptr, nullptr}};
  luaL_newlib(L, ayar);
  lua_setglobal(L, "ayar");

  const luaL_Reg sistem[] = {{"bildir", l_bildir}, {"cikis", l_cikis}, {"isik", l_isik}, {"led", l_led}, {"bellek", l_bellek}, {nullptr, nullptr}};
  luaL_newlib(L, sistem);
  lua_setglobal(L, "sistem");

  lua_pushcfunction(L, l_yaz_seri);
  lua_setglobal(L, "print");
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
  int r = luaL_loadbuffer(LS, src.c_str(), src.length(), chunk.c_str());
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

static void luaOpenFn() {
  String path = luaCur >= 0 && luaCur < (int)luaApps.size() ? luaApps[luaCur].path : String("");
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
  luaRunName = luaCur >= 0 && luaCur < (int)luaApps.size() ? luaApps[luaCur].name : String("Uygulama");
  luaRunBg = luaCur >= 0 && luaCur < (int)luaApps.size() && luaApps[luaCur].bg;
  luaErrNew = luaDrawReq = luaExitReq = luaRawInput = luaHasStep = luaQuit = luaErrPending = false;
  luaFg = true;
#ifdef HOST
  luaProcP = nullptr;
  luaProcInit();
#else
  String stem = baseName(path);
  stem = stem.substring(0, stem.lastIndexOf('.'));
  String n = "lua:" + stem;
  luaPid = kSpawn(n.c_str(), luaProcMain, 14 * 1024, true);
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
}

static const App APP_LUA = {"Uygulama", "Uygulama", I_CODE, luaOpenFn, luaDraw, luaTap, nullptr, luaSwipe, luaRaw, luaTick, nullptr, luaCloseFn};

static void luaAppOpen(int i) {
  if (i < 0 || i >= (int)luaApps.size()) return;
  if (kAlive(luaPid) && luaApps[i].path != luaRunPath && !luaStopProc()) {  // arka plandaki başka uygulama
    toast("Önceki uygulama kapanıyor, biraz bekle");
    return;
  }
  luaCur = i;
  appOpen(&APP_LUA);
}

// ============================================================================
// Örnek uygulamalar ve rehber (klasör yoksa bir kez yazılır)
// ============================================================================
static const char LUA_EX_MERHABA[] PROGMEM = R"(-- ad: Merhaba
-- simge: kod
-- İlk Lua uygulaman: ekrana dokundukça sayı artar.
local sayi = 0

function ciz()
  ekran.temizle()
  ekran.yazi("Merhaba DAXEN!", ekran.en / 2, 64, renk.gumus, 3, "orta")
  ekran.yazi("Ekrana dokun", ekran.en / 2, 92, renk.gri, 2, "orta")
  ekran.yazi(tostring(sayi), ekran.en / 2, 130, renk.vurgu, 5, "orta")
end

function dokun(x, y)
  sayi = sayi + 1
  ciz()
end
)";

static const char LUA_EX_ZAR[] PROGMEM = R"(-- ad: Zar
-- simge: zar
-- İki zar atar. Dokun: at.
local d1, d2 = 1, 1
local kalan = 0

local noktalar = {
  {{0, 0}},
  {{-1, -1}, {1, 1}},
  {{-1, -1}, {0, 0}, {1, 1}},
  {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
  {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
  {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}},
}

local function zar(cx, cy, n)
  ekran.dikdortgen(cx - 40, cy - 40, 80, 80, renk.yuzey, true)
  ekran.dikdortgen(cx - 40, cy - 40, 80, 80, renk.cizgi, false)
  for _, p in ipairs(noktalar[n]) do
    ekran.daire(cx + p[1] * 22, cy + p[2] * 22, 7, renk.gumus, true)
  end
end

function ciz()
  ekran.temizle()
  zar(105, 110, d1)
  zar(215, 110, d2)
  ekran.yazi("Toplam " .. (d1 + d2), 160, 170, renk.gumus, 3, "orta")
  ekran.yazi("Atmak için dokun", 160, 204, renk.soluk, 1, "orta")
end

function dokun()
  kalan = 10
end

function adim(dt)
  if kalan > 0 then
    kalan = kalan - 1
    d1, d2 = math.random(6), math.random(6)
    ciz()
  end
end
)";

static const char LUA_EX_SAYAC[] PROGMEM = R"(-- ad: Sayaç
-- simge: liste
-- Kaldığı yeri hatırlayan sayaç. Alttaki düğmeler: -1 / +1, sayıya dokun: sıfırla.
local n = tonumber(ayar.al("n", "0")) or 0

local function dugme(x, y, w, h, yazi)
  ekran.dikdortgen(x, y, w, h, renk.yuzey, true)
  ekran.dikdortgen(x, y, w, h, renk.cizgi, false)
  ekran.yazi(yazi, x + w / 2, y + h / 2 - 11, renk.gumus, 3, "orta", renk.yuzey)
end

function ciz()
  ekran.temizle()
  ekran.yazi(tostring(n), 160, 56, renk.gumus, 5, "orta")
  ekran.yazi("Sıfırlamak için sayıya dokun", 160, 134, renk.soluk, 1, "orta")
  dugme(20, 168, 130, 52, "−1")
  dugme(170, 168, 130, 52, "+1")
end

function dokun(x, y)
  if y > 160 then
    if x < 160 then n = n - 1 else n = n + 1 end
  else
    n = 0
  end
  ayar.kaydet("n", n)
  ciz()
end
)";

static const char LUA_EX_TOP[] PROGMEM = R"(-- ad: Top
-- simge: oyun
-- Yerçekimli zıplayan top. Parmağınla fırlat.
local x, y, vx, vy = 160, 100, 3, 0
local r = 12
local sx, sy, st = 0, 0, 0

local function top(c)
  ekran.daire(math.floor(x), math.floor(y), r, c, true)
end

function ciz()
  ekran.temizle()
  ekran.yazi("Sürükleyip bırak: fırlat", 160, 40, renk.soluk, 1, "orta")
  top(renk.vurgu)
end

function adim(dt)
  top(renk.zemin)
  vy = vy + 0.4
  x, y = x + vx, y + vy
  if x < r then x = r; vx = -vx * 0.9 end
  if x > ekran.en - r then x = ekran.en - r; vx = -vx * 0.9 end
  if y > ekran.boy - r then y = ekran.boy - r; vy = -vy * 0.82; vx = vx * 0.98 end
  if y < ekran.ust + r + 24 then y = ekran.ust + r + 24; vy = -vy end
  top(renk.vurgu)
end

function bas(px, py)
  sx, sy, st = px, py, zaman.ms()
end

function birak(px, py)
  local t = math.max(30, zaman.ms() - st)
  vx = (px - sx) / t * 12
  vy = (py - sy) / t * 12
end
)";

static const char LUA_EX_ZAMAN[] PROGMEM = R"(-- ad: Zamanlayıcı
-- simge: kronometre
-- arkaplan: evet
-- Geri sayım. Uygulamadan çıksan da arka planda sayar, bitince haber verir.
local sure = 5 * 60
local kalan = sure
local calisiyor = false
local son = zaman.ms()

local function iki(n) return string.format("%02d", n) end

local function dugme(x, y, w, h, yazi, vurgu)
  local zemin = vurgu and renk.vurgu or renk.yuzey
  ekran.dikdortgen(x, y, w, h, zemin, true)
  ekran.dikdortgen(x, y, w, h, renk.cizgi, false)
  ekran.yazi(yazi, x + w / 2, y + h / 2 - 8, vurgu and renk.zemin or renk.gumus, 2, "orta", zemin)
end

local function sayi()
  ekran.dikdortgen(20, 44, 280, 76, renk.zemin, true)
  ekran.yazi(iki(kalan // 60) .. ":" .. iki(kalan % 60), 160, 50, calisiyor and renk.vurgu or renk.gumus, 5, "orta")
end

function ciz()
  ekran.temizle()
  sayi()
  ekran.yazi(calisiyor and "Çıksan da sayar" or "Süreyi ayarla, başlat", 160, 134, renk.soluk, 1, "orta")
  dugme(16, 168, 90, 48, "−1 dk")
  dugme(115, 168, 90, 48, calisiyor and "Durdur" or "Başlat", true)
  dugme(214, 168, 90, 48, "+1 dk")
end

function dokun(x, y)
  if y < 160 then return end
  if x < 110 then
    sure = math.max(60, sure - 60); kalan = sure; calisiyor = false
  elseif x < 210 then
    if kalan == 0 then kalan = sure end
    calisiyor = not calisiyor
    son = zaman.ms()
  else
    sure = math.min(99 * 60, sure + 60); kalan = sure; calisiyor = false
  end
  ciz()
end

function adim(dt)
  if not calisiyor then return end
  if zaman.ms() - son < 1000 then return end
  son = son + 1000
  kalan = kalan - 1
  if kalan <= 0 then
    kalan = 0
    calisiyor = false
    sistem.bildir("Zamanlayıcı: süre doldu")
    sistem.led(0, 0, 255)
    ciz()
  else
    sayi()
  end
end
)";

static const char LUA_GUIDE[] PROGMEM = R"(DAXEN VANE · Kendi uygulamanı yaz (Lua)
=========================================

Bu klasördeki her .lua dosyası Uygulamalar ekranında bir uygulama olarak görünür.
Dosyanın ilk satırları uygulamanın adını ve simgesini söyler:

  -- ad: Benim Uygulamam
  -- simge: oyun
  -- arkaplan: evet      (isteğe bağlı: çıkınca kapanmaz, arka planda sürer)

Her uygulama ayrı bir süreç olarak çalışır: uzun bir hesap yapsa da saat, dokunma ve alarmlar
donmaz. Çalışan süreçleri Görevler uygulamasında görür, istersen sonlandırırsın. Arka planda
süren uygulama ekrana çizemez ama adim() ve sistem.bildir() çalışır; tekrar açınca ciz() gelir.

Simgeler: kod oyun zar saat alarm not liste hesap grafik muzik kalem kitap isik hedef
kronometre dunya bulut gunes ates bayrak kart yilan foto klavye cip

Uygulama, tanımladığın işlevlerle çalışır (hepsi isteğe bağlı):
  baslat()          açılınca bir kez
  ciz()             ekran baştan çizilecekken
  dokun(x, y)       ekrana dokunup bırakınca
  kaydir(yon)       "sol", "sag", "yukari", "asagi"
  bas(x, y), surukle(x, y), birak(x, y)
                    parmağı izlemek için (tanımlarsan dokun/kaydir gelmez)
  adim(dt)          saniyede ~30 kez; dt: geçen milisaniye (animasyon için)
  kapat()           çıkarken

Ekran 320 x 240. Üstteki 30 piksel başlık çubuğudur (ekran.ust); sol üst köşe geri tuşudur.
  ekran.temizle([renk])
  ekran.dikdortgen(x, y, en, boy, renk, [dolu=true])
  ekran.cizgi(x1, y1, x2, y2, renk, [kalınlık=1])
  ekran.daire(x, y, r, renk, [dolu=true])
  ekran.piksel(x, y, renk)
  ekran.yazi(metin, x, y, [renk], [boyut 1-5], ["sol"|"orta"|"sag"], [arka renk])
      boyut 1: küçük, 2: normal, 3: büyük, 4 ve 5: yalnızca rakamlar (çok büyük)
  ekran.genislik(metin, [boyut])
  ekran.renk(k, y, m)              0-255 değerlerden renk
  ekran.simge(ad, x, y, [boyut], [renk])
  ekran.en, ekran.boy, ekran.ust

Renkler: renk.zemin yuzey yuzey2 gumus gri soluk cizgi vurgu siyah beyaz
         kirmizi turuncu sari yesil mavi mor

  zaman.ms()                 açılıştan beri milisaniye
  zaman.saat()               saat, dakika, saniye
  zaman.tarih()              yıl, ay, gün, haftanın günü (0 = pazar)

Dosyalar yalnızca uygulamanın kendi klasöründedir (uygulamalar/veri/<dosya adı>/):
  dosya.oku(ad)   dosya.yaz(ad, metin)   dosya.ekle(ad, metin)
  dosya.var(ad)   dosya.sil(ad)          dosya.liste()
  ayar.al(ad, varsayılan)    ayar.kaydet(ad, değer)

  sistem.bildir(metin)       alttaki balon
  sistem.cikis()             uygulamayı kapatır
  sistem.isik()              ortam ışığı (%)
  sistem.led(k, y, m)        kartın RGB ışığı, kısa süre
  sistem.bellek()            uygulamanın kalan belleği (bayt)

Lua'nın string, table, math, utf8 ve coroutine kitaplıkları vardır (io ve os yok).
Takılan bir uygulamadan geri tuşuyla çıkabilirsin; süreç sonlandırılır.
Hata olursa ekranda satır numarasıyla görünür.
Örnekler: merhaba.lua, zar.lua, sayac.lua, top.lua, zamanlayici.lua (arka planda sayar). Değiştirip dene!
)";

static void luaWriteFile(const char* path, const char* data) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  f.write((const uint8_t*)data, strlen(data));
  f.close();
}

static void luaEnsureExamples() {
  if (!SD.exists(LUA_APPS_DIR)) {
    if (!SD.exists("/daxen")) SD.mkdir("/daxen");
    SD.mkdir(LUA_APPS_DIR);
    luaWriteFile(LUA_APPS_DIR "/merhaba.lua", LUA_EX_MERHABA);
    luaWriteFile(LUA_APPS_DIR "/zar.lua", LUA_EX_ZAR);
    luaWriteFile(LUA_APPS_DIR "/sayac.lua", LUA_EX_SAYAC);
    luaWriteFile(LUA_APPS_DIR "/top.lua", LUA_EX_TOP);
  }
  // 2. sürüm: süreçler ve arka plan (yeni örnek ve güncel rehber, bir kez)
  if (!SD.exists(LUA_APPS_DIR "/veri/.surum2")) {
    if (!SD.exists(LUA_APPS_DIR "/zamanlayici.lua")) luaWriteFile(LUA_APPS_DIR "/zamanlayici.lua", LUA_EX_ZAMAN);
    luaWriteFile(LUA_APPS_DIR "/REHBER.txt", LUA_GUIDE);
    if (!SD.exists(LUA_APPS_DIR "/veri")) SD.mkdir(LUA_APPS_DIR "/veri");
    luaWriteFile(LUA_APPS_DIR "/veri/.surum2", "2");
  }
}
