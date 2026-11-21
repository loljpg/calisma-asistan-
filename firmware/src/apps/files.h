#pragma once
// Dosyalar: SD kart gezgini. Klasörlere gir, dosyayı türüne göre aç; basılı tut: yeniden adlandır,
// kopyala, kes, sil, bilgi. Başlıkta yeni klasör ve yapıştır.
#include "../os/shell.h"
#include <SD.h>

struct FEntry {
  String name;
  bool dir = false;
  uint32_t size = 0;
};
static String fCwd = "/";
static std::vector<FEntry> fList;
static int fFirst = 0;
static String fClip;        // kopyalanan ya da kesilen yol
static bool fClipCut = false;
static const int F_ROW = 32, F_ROWS = 6;

static uint8_t fileIcon(const String& name, bool dir) {
  if (dir) return I_FOLDER;
  String e = extOf(name);
  if (e == "txt" || e == "md" || e == "csv" || e == "log" || e == "json") return I_NOTES;
  if (e == "mp3" || e == "wav") return I_NOTE;
  if (e == "jpg" || e == "jpeg" || e == "bmp" || e == "cizim") return I_PHOTO;
  if (e == "lua") return I_CODE;
  if (e == "bin") return I_CHIP;
  if (e == "el") return I_PENCIL;
  return I_FILE;
}

static void fLoad() {
  fList.clear();
  if (!sdOk) return;
  File d = SD.open(fCwd);
  if (!d || !d.isDirectory()) {
    fCwd = "/";
    d = SD.open("/");
    if (!d) return;
  }
  for (File f = d.openNextFile(); f && fList.size() < 400; f = d.openNextFile()) {
    FEntry e;
    e.name = baseName(f.name());
    e.dir = f.isDirectory();
    e.size = e.dir ? 0 : f.size();
    f.close();
    if (e.name.startsWith(".") || e.name == "System Volume Information") continue;
    fList.push_back(e);
  }
  d.close();
  std::sort(fList.begin(), fList.end(), [](const FEntry& a, const FEntry& b) {
    if (a.dir != b.dir) return a.dir;
    return trLower(a.name) < trLower(b.name);
  });
}

static String fTitle() { return fCwd == "/" ? String("SD kart") : baseName(fCwd); }

static void fDrawList() {
  if (!sdOk) {
    emptyState(HDR_H, SCR_H - HDR_H, I_SD, "SD kart takılı değil", "Kartı tak ve cihazı yeniden başlat.");
    return;
  }
  int n = fList.size();
  fFirst = constrain(fFirst, 0, std::max(0, n - F_ROWS));
  for (int r = 0; r < F_ROWS; r++) {
    int y = HDR_H + r * F_ROW, i = fFirst + r;
    fillBg(0, y, SCR_W, F_ROW);
    if (i >= n) continue;
    const FEntry& e = fList[i];
    bool clip = fClip.length() && fClip == joinPath(fCwd, e.name);
    drawIcon(fileIcon(e.name, e.dir), 12, y + 7, 18, clip ? C_ACC : (e.dir ? C_TEXT : C_TEXT2), C_BG);
    String sz = e.dir ? String("") : fmtBytes(e.size);
    int sw = sz.length() ? textW(sz, F_XS) + 10 : (e.dir ? 18 : 0);
    text(fit(e.name, F_SM, 262 - sw), 40, y + F_ROW / 2 + 1, F_SM, C_TEXT, C_BG, ML_DATUM);
    if (e.dir) drawIcon(I_RIGHT, 296, y + 10, 12, C_TEXT3, C_BG);
    else text(sz, 308, y + F_ROW / 2 + 1, F_XS, C_TEXT3, C_BG, MR_DATUM);
    hline(40, y + F_ROW - 1, 268);
  }
  fillBg(0, HDR_H + F_ROW * F_ROWS, SCR_W, SCR_H - HDR_H - F_ROW * F_ROWS);
  String foot = fCwd + " · " + String(n) + " öğe";
  if (fClip.length()) foot = (fClipCut ? "Kesildi: " : "Kopyalandı: ") + baseName(fClip);
  text(fit(foot, F_XS, 296), 12, SCR_H - 9, F_XS, fClip.length() ? C_ACC : C_TEXT3, C_BG, ML_DATUM);
  if (!n) text("Klasör boş", 160, HDR_H + 80, F_SM, C_TEXT3, C_BG, MC_DATUM);
  scrollBar(HDR_H, F_ROW * F_ROWS, fFirst, F_ROWS, n);
}

static void fDraw(bool full) {
  if (full) {
    header(fTitle());
    if (sdOk) {
      headerAction(0, I_PLUS);
      if (fClip.length()) headerAction(1, I_PASTE, true);
    }
  }
  fDrawList();
}

static void fEnter(const String& dir) {
  fCwd = dir;
  fFirst = 0;
  fLoad();
  fDraw(true);
}

// Dosya ya da klasörü (içiyle) siler
static bool fRemoveAll(const String& path) {
  File f = SD.open(path);
  if (!f) return false;
  bool dir = f.isDirectory();
  if (!dir) {
    f.close();
    return SD.remove(path);
  }
  std::vector<String> kids;
  for (File k = f.openNextFile(); k; k = f.openNextFile()) {
    kids.push_back(joinPath(path, baseName(k.name())));
    k.close();
  }
  f.close();
  for (auto& k : kids) fRemoveAll(k);
  return SD.rmdir(path);
}

static bool fCopyFile(const String& from, const String& to) {
  File a = SD.open(from, FILE_READ);
  if (!a) return false;
  File b = SD.open(to, FILE_WRITE);
  if (!b) {
    a.close();
    return false;
  }
  uint8_t buf[512];
  size_t n;
  bool ok = true;
  while ((n = a.read(buf, sizeof buf)) > 0)
    if (b.write(buf, n) != n) {
      ok = false;
      break;
    }
  a.close();
  b.close();
  return ok;
}

static void fPaste() {
  String name = baseName(fClip), dst = joinPath(fCwd, name);
  if (dst == fClip) {
    if (fClipCut) {
      fClip = "";
      fDraw(true);
      return;
    }
    int dot = name.lastIndexOf('.');
    String stem = dot > 0 ? name.substring(0, dot) : name, ext = dot > 0 ? name.substring(dot) : String("");
    dst = joinPath(fCwd, stem + " kopya" + ext);
  }
  if (SD.exists(dst)) {
    toast("Bu adda bir öğe zaten var");
    return;
  }
  File f = SD.open(fClip);
  bool isDir = f && f.isDirectory();
  if (f) f.close();
  bool ok;
  if (fClipCut) ok = SD.rename(fClip, dst);
  else if (isDir) {
    toast("Klasörler yalnızca taşınabilir (Kes)");
    return;
  } else {
    toast("Kopyalanıyor…");
    ok = fCopyFile(fClip, dst);
  }
  if (ok) {
    fClip = "";
    fLoad();
    fDraw(true);
    toast(fClipCut ? "Taşındı" : "Kopyalandı");
  } else {
    toast("İşlem yapılamadı");
  }
}

static void fItemMenu(int i) {
  FEntry e = fList[i];
  String path = joinPath(fCwd, e.name);
  std::vector<String> opts = {"Aç", "Yeniden adlandır", "Kopyala", "Kes", "Sil"};
  menu(e.name, opts, [e, path](int c) {
    switch (c) {
      case 0:
        if (e.dir) fEnter(path);
        else openFileWith(path);
        break;
      case 1:
        textInput("Yeni ad", e.name, [path](const String& v) {
          String nn = safeName(v);
          if (!nn.length()) return;
          String dst = joinPath(dirName(path), nn);
          if (SD.exists(dst)) toast("Bu adda bir öğe zaten var");
          else if (SD.rename(path, dst)) {
            fLoad();
            fDraw(true);
          } else toast("Yeniden adlandırılamadı");
        }, false, 60);
        break;
      case 2:
      case 3:
        fClip = path;
        fClipCut = c == 3;
        fDraw(true);
        toast("Hedef klasörde başlıktaki yapıştıra dokun");
        break;
      case 4:
        dialog(e.dir ? "Klasör silinsin mi?" : "Dosya silinsin mi?",
               e.dir ? e.name + " ve içindeki her şey silinecek." : e.name + " silinecek.", {"Vazgeç", "Sil"}, [path](int k) {
                 if (k != 1) return;
                 if (fRemoveAll(path)) {
                   fLoad();
                   fDraw(true);
                   toast("Silindi");
                 } else {
                   toast("Silinemedi");
                 }
               });
        break;
      default: break;
    }
  });
}

static void fTap(int x, int y) {
  if (y < HDR_H) {
    int h = headerHit(x, y);
    if (h == 0 && sdOk) {
      textInput("Yeni klasör", "", [](const String& v) {
        String nn = safeName(v);
        if (!nn.length()) return;
        if (SD.mkdir(joinPath(fCwd, nn))) {
          fLoad();
          fDraw(false);
        } else toast("Klasör oluşturulamadı");
      }, false, 40, "Klasör adı");
    } else if (h == 1 && fClip.length()) {
      fPaste();
    }
    return;
  }
  int r = (y - HDR_H) / F_ROW, i = fFirst + r;
  if (r >= F_ROWS || i >= (int)fList.size()) return;
  const FEntry& e = fList[i];
  if (e.dir) fEnter(joinPath(fCwd, e.name));
  else openFileWith(joinPath(fCwd, e.name));
}

static bool fHold(int x, int y) {
  int r = (y - HDR_H) / F_ROW, i = fFirst + r;
  if (y < HDR_H || r >= F_ROWS || i >= (int)fList.size()) return false;
  fItemMenu(i);
  return true;
}

static void fSwipe(int dir) {
  if (dir != SW_UP && dir != SW_DOWN) return;
  int n = fList.size();
  int nf = constrain(fFirst + (dir == SW_UP ? F_ROWS - 1 : -(F_ROWS - 1)), 0, std::max(0, n - F_ROWS));
  if (nf != fFirst) {
    fFirst = nf;
    fDrawList();
  }
}

static bool fBack() {
  if (fCwd != "/") {
    String up = dirName(fCwd);
    String from = baseName(fCwd);
    fCwd = up;
    fLoad();
    for (size_t i = 0; i < fList.size(); i++)
      if (fList[i].name == from) fFirst = std::max(0, (int)i - 2);
    fDraw(true);
    return true;
  }
  return false;
}

static void fOpen() {
  fLoad();
  if (fList.empty() && fCwd != "/") {
    fCwd = "/";
    fLoad();
  }
}

// Müzik yazılımı kaldırıldı: SD'de kalan eski modül dosyası kendiliğinden silinmez, bir kez sorulur.
#define OLD_MUSIC_FILE "/daxen/sys/muzik-" FW_VARIANT ".bin"
static void fOfferOldMusicModule() {
  if (!sdOk || !SD.exists(OLD_MUSIC_FILE)) return;
  bool asked;
  {
    Lock l;
    prefs.begin("cs", false);
    asked = prefs.getBool("musq", false);
    if (!asked) prefs.putBool("musq", true);
    prefs.end();
  }
  if (asked) return;
  File f = SD.open(OLD_MUSIC_FILE, FILE_READ);
  uint64_t size = f ? f.size() : 0;
  if (f) f.close();
  dialog("Eski müzik modülü", "Müzik kaldırıldı. Modül dosyası SD kartta " + fmtBytes(size) + " yer kaplıyor. Silinsin mi?",
         {"Kalsın", "Sil"}, [](int c) {
           if (c != 1) return;
           toast(SD.remove(OLD_MUSIC_FILE) ? "Eski müzik modülü silindi" : "Silinemedi");
         });
}

static void fClose() { std::vector<FEntry>().swap(fList); }  // klasör listesi bırakılır (açılışta yeniden okunur)

static const App APP_FILES = {"Dosyalar", "Dosyalar", I_FOLDER, fOpen, fDraw, fTap, fHold, fSwipe, nullptr, nullptr, fBack, fClose};
