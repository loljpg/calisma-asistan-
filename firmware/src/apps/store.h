#pragma once
// Mağaza: yazılımla gelen isteğe bağlı Lua uygulamaları (kaynakları firmware/lua/magaza/). SD'deki
// varlık paketinde dururlar; flash'ta yer tutmazlar. Kur: uygulama klasörüne kopyalanır, ilk açılışta
// izinleri sorulur. Yeni sürümle uygulama değiştiyse "Güncelle" görünür (kurulu dosyanın CRC32'si
// paketteki ile karşılaştırılır). Basılı tut: kaldır.
#include "../os/shell.h"

struct StoreApp {
  String file, name, desc, ver;
  uint8_t icon = I_CODE, perm = 0;
  uint8_t state = 0;  // 0 kurulu değil, 1 kurulu, 2 güncelleme var
};
static std::vector<StoreApp> stApps;
static int stFirst = 0;
static const int ST_ROW = 52, ST_ROWS = 4;

static String stPath(const StoreApp& a) { return joinPath(LUA_APPS_DIR, a.file); }

static void stState(StoreApp& a) {
  String p = stPath(a);
  if (!SD.exists(p)) {
    a.state = 0;
    return;
  }
  uint32_t crc, size;
  a.state = packCrc(("magaza/" + a.file).c_str(), crc, size) && luaFileCrc(p) != crc ? 2 : 1;
}

static void stLoad() {
  stApps.clear();
  if (!packOk) return;
  for (const String& f : packList("magaza/")) {
    StoreApp a;
    a.file = f;
    a.name = f.substring(0, f.lastIndexOf('.'));
    char head[600];
    size_t n = packHead(("magaza/" + f).c_str(), head, sizeof head - 1);
    head[n] = 0;
    LuaAppInfo inf;
    inf.name = a.name;
    inf.perm = 0;
    const char* p = head;
    for (int line = 0; line < 10 && *p; line++) {
      const char* e = strchr(p, '\n');
      String l = e ? String(p).substring(0, e - p) : String(p);
      if (!l.startsWith("--")) break;
      luaParseHeader(inf, l);
      String body = l.substring(2);
      body.trim();
      if (body.startsWith("aciklama:")) {
        a.desc = body.substring(9);
        a.desc.trim();
      }
      if (!e) break;
      p = e + 1;
    }
    a.name = inf.name;
    a.icon = inf.icon;
    a.perm = inf.perm;
    a.ver = inf.ver;
    stState(a);
    stApps.push_back(a);
  }
}

static void stDrawRow(int r) {
  int i = stFirst + r, y = HDR_H + r * ST_ROW;
  fillBg(0, y, SCR_W, ST_ROW);
  if (i >= (int)stApps.size()) return;
  const StoreApp& a = stApps[i];
  panel(12, y + 10, 32, 32, C_SURF, C_LINE);
  drawIcon(a.icon, 18, y + 16, 20, C_TEXT, C_SURF);
  text(fit(a.name, F_SM, 120), 54, y + 4, F_SM, C_TEXT, C_BG);
  if (a.ver.length()) text(a.ver, 54 + textW(fit(a.name, F_SM, 120), F_SM) + 6, y + 7, F_XS, C_TEXT3, C_BG);
  auto ls = wrapText(a.desc, F_XS, 186, 2);
  for (size_t k = 0; k < ls.size(); k++) text(ls[k], 54, y + 22 + k * 13, F_XS, C_TEXT2, C_BG);
  static const char* const L[] = {"Kur", "Kurulu", "Güncelle"};
  button(248, y + 12, 62, 28, L[a.state], a.state == 1 ? B_GHOST : B_PRIMARY);
  hline(54, y + ST_ROW - 1, SCR_W - 66);
}

static void stDraw(bool full) {
  if (full) header("Mağaza");
  if (!packOk) {
    emptyState(HDR_H, SCR_H - HDR_H, I_SD, "Varlık paketi yok",
               sdOk ? "Kart Wi-Fi'deyken paketi indirir; sonra burada görünür." : "SD kart gerekli.");
    return;
  }
  if (stApps.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_APPS, "Uygulama yok", "Bu sürümde mağazada uygulama yok.");
    return;
  }
  stFirst = constrain(stFirst, 0, std::max(0, (int)stApps.size() - ST_ROWS));
  for (int r = 0; r < ST_ROWS; r++) stDrawRow(r);
  fillBg(0, HDR_H + ST_ROWS * ST_ROW, SCR_W, SCR_H - HDR_H - ST_ROWS * ST_ROW);
  scrollBar(HDR_H, ST_ROW * ST_ROWS, stFirst, ST_ROWS, stApps.size());
}

static void stInstall(int i) {
  StoreApp& a = stApps[i];
  luaAppCount();  // klasör yoksa oluşur
  if (!packCopy(("magaza/" + a.file).c_str(), stPath(a))) {
    toast("Kurulamadı (SD kart?)");
    return;
  }
  luaScanned = false;
  sysLog("mağaza", (a.state == 2 ? "güncellendi: " : "kuruldu: ") + a.file);
  toast(a.state == 2 ? a.name + " güncellendi" : a.name + " kuruldu: Uygulamalar'da");
  stState(a);
}

static void stTap(int x, int y) {
  if (y < HDR_H) return;
  int r = (y - HDR_H) / ST_ROW, i = stFirst + r;
  if (r >= ST_ROWS || i >= (int)stApps.size()) return;
  const StoreApp& a = stApps[i];
  if (x >= 240 && a.state != 1) {
    stInstall(i);
    stDrawRow(r);
  } else {
    alertBox(a.name, a.desc + (a.perm ? "\nİzinler: " + luaPermText(a.perm) : String("")));
  }
}

static bool stHold(int x, int y) {
  if (y < HDR_H) return false;
  int r = (y - HDR_H) / ST_ROW, i = stFirst + r;
  if (r >= ST_ROWS || i >= (int)stApps.size() || stApps[i].state == 0) return false;
  String p = stPath(stApps[i]), name = stApps[i].name;
  dialog(name + " kaldırılsın mı?", "Uygulamanın kayıtlı verileri (veri klasörü) silinmez.", {"Vazgeç", "Kaldır"}, [p, name](int c) {
    if (c != 1) return;
    SD.remove(p);
    luaScanned = false;
    sysLog("mağaza", "kaldırıldı: " + baseName(p));
    for (auto& a : stApps) stState(a);
  });
  return true;
}

static void stSwipe(int dir) {
  int n = stApps.size();
  int nf = constrain(stFirst + (dir == SW_UP ? ST_ROWS - 1 : (dir == SW_DOWN ? -(ST_ROWS - 1) : 0)), 0, std::max(0, n - ST_ROWS));
  if (nf != stFirst) {
    stFirst = nf;
    stDraw(false);
  }
}

static void stOpen() {
  stFirst = 0;
  stLoad();
}
static void stClose() { std::vector<StoreApp>().swap(stApps); }

static const App APP_STORE = {"Mağaza", "Mağaza", I_APPS, stOpen, stDraw, stTap, stHold, stSwipe, nullptr, nullptr, nullptr, stClose};
