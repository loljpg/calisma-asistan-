#pragma once
// Okuyucu: SD karttaki metin dosyaları (.txt, .md, ...). Sağa dokun: sonraki sayfa, sola: önceki.
// Ortaya dokun: yazı boyutu ve dosya bilgisi. Her dosyada kaldığın yer hatırlanır.
#include "../os/shell.h"
#include <SD.h>

#define BOOKS_DIR "/daxen/kitaplar"
static std::vector<String> rdList;
static int rdFirst = 0;
static String rdPath, rdOpenPath;
static File rdFile;
static uint32_t rdSize = 0, rdPos = 0, rdNext = 0;
static std::vector<uint32_t> rdBack;  // önceki sayfaların başları
static bool rdBig = false, rdBar = false;
static const int RD_ROW = 32, RD_ROWS = 6;

static uint32_t rdKey(const String& p) {  // dosya yolu için kısa anahtar
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < p.length(); i++) h = (h ^ (uint8_t)p[i]) * 16777619u;
  return h;
}

static void rdSavePos() {
  if (!rdPath.length()) return;
  char k[12];
  snprintf(k, sizeof k, "p%08x", (unsigned)rdKey(rdPath));
  Lock l;
  prefs.begin("reader", false);
  prefs.putUInt(k, rdPos);
  prefs.putBool("big", rdBig);
  prefs.end();
}

static uint32_t rdLoadPos(const String& p) {
  char k[12];
  snprintf(k, sizeof k, "p%08x", (unsigned)rdKey(p));
  Lock l;
  prefs.begin("reader", true);
  uint32_t v = prefs.getUInt(k, 0);
  rdBig = prefs.getBool("big", false);
  prefs.end();
  return v;
}

// Bir sayfayı çizer (ya da yalnızca ölçer); bir sonraki sayfanın başını döner.
static uint32_t rdPage(uint32_t start, bool draw) {
  const uint8_t* f = rdBig ? F_MD : F_SM;
  int lh = rdBig ? 22 : 18, top = 8, bottom = SCR_H - 18, maxW = SCR_W - 24;
  int lines = (bottom - top) / lh;
  rdFile.seek(start);
  uint32_t pos = start;
  if (draw) fillBg(0, 0, SCR_W, bottom + 2);
  for (int ln = 0; ln < lines && pos < rdSize; ln++) {
    // satır: sözcük sınırında kır
    String line;
    uint32_t lineStart = pos, lastBreak = 0;
    int lastBreakLen = 0;
    while (pos < rdSize) {
      int c = rdFile.read();
      if (c < 0) break;
      pos++;
      if (c == '\r') continue;
      if (c == '\n') break;
      if ((c & 0xC0) == 0x80) {  // UTF-8 devam baytı
        line += (char)c;
        continue;
      }
      // çok baytlı karakterin tamamını al
      String ch((char)c);
      int n = utf8Len((uint8_t)c);
      for (int k = 1; k < n && pos < rdSize; k++) {
        ch += (char)rdFile.read();
        pos++;
      }
      String cand = line + ch;
      if (textW(cand, f) > maxW) {
        if (c == ' ') break;  // satır sonundaki boşluk yutulur
        if (lastBreak) {      // son boşluktan kır
          pos = lastBreak;
          line = line.substring(0, lastBreakLen);
        } else {
          pos -= ch.length();
        }
        rdFile.seek(pos);
        break;
      }
      line = cand;
      if (c == ' ') {
        lastBreak = pos;
        lastBreakLen = line.length();
      }
    }
    if (draw && line.length()) text(line, 12, top + ln * lh, f, C_TEXT, C_BG);
    if (pos == lineStart) break;
  }
  return pos;
}

static void rdDrawStatus() {
  fillBg(0, SCR_H - 16, SCR_W, 16);
  int pct = rdSize ? (int)((uint64_t)rdNext * 100 / rdSize) : 100;
  progress(12, SCR_H - 4, 296, 1, pct, C_TEXT3);
  text(fit(baseName(rdPath), F_XS, 220), 12, SCR_H - 12, F_XS, C_TEXT3, C_BG);
  text("%" + String(pct), 308, SCR_H - 12, F_XS, C_TEXT3, C_BG, TR_DATUM);
}

static void rdShowPage() {
  rdNext = rdPage(rdPos, true);
  rdDrawStatus();
  if (rdBar) {
    panel(40, 70, 240, 100, C_SURF, C_LINE);
    text("Yazı boyutu", 56, 84, F_SM, C_TEXT2, C_SURF);
    button(56, 108, 100, 30, "Küçük", rdBig ? B_NORMAL : B_ON, I_NONE, C_SURF);
    button(164, 108, 100, 30, "Büyük", rdBig ? B_ON : B_NORMAL, I_NONE, C_SURF);
    text(fmtBytes(rdSize), 56, 148, F_XS, C_TEXT3, C_SURF);
    text("Başa dön", 264, 148, F_XS, C_ACC, C_SURF, TR_DATUM);
  }
}

static bool rdOpenFile(const String& p) {
  if (rdFile) rdFile.close();
  rdFile = SD.open(p);
  if (!rdFile || rdFile.isDirectory()) return false;
  rdPath = p;
  rdSize = rdFile.size();
  rdPos = rdLoadPos(p);
  if (rdPos >= rdSize) rdPos = 0;
  rdBack.clear();
  rdBar = false;
  return true;
}

static void rdScan(const String& dir) {
  File d = SD.open(dir);
  if (!d) return;
  for (File f = d.openNextFile(); f && rdList.size() < 200; f = d.openNextFile()) {
    String n = baseName(f.name()), e = extOf(n);
    if (!f.isDirectory() && (e == "txt" || e == "md")) rdList.push_back(joinPath(dir, n));
    f.close();
  }
  d.close();
}

static void rdDraw(bool full) {
  if (rdFile) {
    rdShowPage();
    return;
  }
  if (full) header("Okuyucu");
  if (rdList.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_BOOK, "Kitap yok",
               "Metin dosyalarını SD karttaki daxen/kitaplar klasörüne koy ya da Dosyalar'dan bir .txt aç.");
    return;
  }
  int n = rdList.size();
  rdFirst = constrain(rdFirst, 0, std::max(0, n - RD_ROWS));
  for (int r = 0; r < RD_ROWS; r++) {
    int i = rdFirst + r, y = HDR_H + r * RD_ROW;
    fillBg(0, y, SCR_W, RD_ROW);
    if (i >= n) continue;
    drawIcon(I_BOOK, 12, y + 7, 18, C_TEXT2, C_BG);
    String nm = baseName(rdList[i]);
    text(fit(nm.substring(0, nm.lastIndexOf('.')), F_SM, 260), 40, y + RD_ROW / 2 + 1, F_SM, C_TEXT, C_BG, ML_DATUM);
    hline(40, y + RD_ROW - 1, 268);
  }
  fillBg(0, HDR_H + RD_ROW * RD_ROWS, SCR_W, SCR_H - HDR_H - RD_ROW * RD_ROWS);
}

static void rdOpen() {
  rdList.clear();
  if (rdOpenPath.length()) {
    String p = rdOpenPath;
    rdOpenPath = "";
    if (rdOpenFile(p)) return;
  }
  if (sdOk) {
    if (!SD.exists(BOOKS_DIR)) {
      if (!SD.exists("/daxen")) SD.mkdir("/daxen");
      SD.mkdir(BOOKS_DIR);
    }
    rdScan(BOOKS_DIR);
    std::sort(rdList.begin(), rdList.end());
  }
}

static void rdTap(int x, int y) {
  if (!rdFile) {
    int r = (y - HDR_H) / RD_ROW, i = rdFirst + r;
    if (y >= HDR_H && r < RD_ROWS && i < (int)rdList.size() && rdOpenFile(rdList[i])) rdShowPage();
    return;
  }
  if (rdBar) {
    if (y >= 104 && y < 142) {
      rdBig = x >= 160;
      rdBack.clear();
    } else if (y >= 142 && y < 166 && x >= 180) {
      rdPos = 0;
      rdBack.clear();
    }
    rdBar = false;
    rdShowPage();
    rdSavePos();
    return;
  }
  if (x > 200) {
    if (rdNext < rdSize) {
      rdBack.push_back(rdPos);
      rdPos = rdNext;
      rdShowPage();
      rdSavePos();
    }
  } else if (x < 120) {
    if (!rdBack.empty()) {
      rdPos = rdBack.back();
      rdBack.pop_back();
    } else if (rdPos > 0) {
      // bilinmeyen önceki sayfa: kabaca geri git, satır başına hizala
      uint32_t guess = rdPos > 1400 ? rdPos - 1400 : 0;
      rdFile.seek(guess);
      while (guess > 0 && guess < rdPos) {
        int c = rdFile.read();
        guess++;
        if (c == '\n' || c == ' ') break;
      }
      rdPos = guess;
    }
    rdShowPage();
    rdSavePos();
  } else {
    rdBar = true;
    rdShowPage();
  }
}

static void rdSwipe(int dir) {
  if (rdFile) {
    if (dir == SW_LEFT) rdTap(300, 120);
    else if (dir == SW_RIGHT) rdTap(10, 120);
  } else if (dir == SW_UP || dir == SW_DOWN) {
    int n = rdList.size();
    rdFirst = constrain(rdFirst + (dir == SW_UP ? RD_ROWS - 1 : -(RD_ROWS - 1)), 0, std::max(0, n - RD_ROWS));
    rdDraw(false);
  }
}

static bool rdBackFn() {
  if (rdFile) {
    rdSavePos();
    rdFile.close();
    rdPath = "";
    if (rdList.empty()) return false;
    rdDraw(true);
    return true;
  }
  return false;
}

static void rdClose() {
  if (rdFile) {
    rdSavePos();
    rdFile.close();
  }
  rdPath = "";
}

// Kitap açıkken başlık yoktur: sol üst köşeye dokunmak da geri götürür (kabuk halleder).
static const App APP_READER = {"Okuyucu", "Okuyucu", I_BOOK, rdOpen, rdDraw, rdTap, nullptr, rdSwipe, nullptr, nullptr, rdBackFn, rdClose};
