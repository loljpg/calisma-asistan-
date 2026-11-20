#pragma once
// Fotoğraflar: SD karttaki JPEG ve BMP resimler ile Çizim dosyaları. Ekrana sığdırılarak gösterilir;
// sağa sola kaydır: önceki / sonraki, dokun: bilgi çubuğu.
#include "../os/shell.h"
#include <SD.h>
#ifndef HOST
#include "rom/tjpgd.h"
#endif

static std::vector<String> phList;
static int phFirst = 0, phIdx = -1;  // phIdx >= 0: görüntüleniyor
static bool phBar = false;
static String phErr;
static String phOpenPath;  // başka uygulamadan açılan
static const int PH_ROW = 32, PH_ROWS = 6;

static bool phIsImage(const String& n) {
  String e = extOf(n);
  return e == "jpg" || e == "jpeg" || e == "bmp" || e == "cizim";
}

static void phScan(const String& dir, int depth) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return;
  std::vector<String> sub;
  for (File f = d.openNextFile(); f && phList.size() < 400; f = d.openNextFile()) {
    String n = baseName(f.name());
    if (!n.startsWith(".")) {
      if (f.isDirectory()) {
        if (depth > 0 && n != "System Volume Information" && n != "calisma" && n != "Muzik") sub.push_back(joinPath(dir, n));
      } else if (phIsImage(n)) {
        phList.push_back(joinPath(dir, n));
      }
    }
    f.close();
  }
  d.close();
  for (auto& s : sub) phScan(s, depth - 1);
}

// --- JPEG (ESP32'nin ROM'undaki TJpgDec) ---
#ifndef HOST
struct PhJpg {
  File* f;
  int ox, oy;
};
static UINT phJpgIn(JDEC* jd, BYTE* buf, UINT n) {
  PhJpg* j = (PhJpg*)jd->device;
  if (buf) return j->f->read(buf, n);
  j->f->seek(j->f->position() + n);
  return n;
}
static UINT phJpgOut(JDEC* jd, void* bmp, JRECT* r) {
  PhJpg* j = (PhJpg*)jd->device;
  int w = r->right - r->left + 1, h = r->bottom - r->top + 1;
  uint8_t* p = (uint8_t*)bmp;
  static uint16_t line[64];
  for (int y = 0; y < h; y++) {
    int sy = j->oy + r->top + y;
    for (int x = 0; x < w && x < 64; x++, p += 3) line[x] = rgbn(p[0], p[1], p[2]);
    if (sy >= 0 && sy < SCR_H) {
      int sx = j->ox + r->left, ww = w, off = 0;
      if (sx < 0) {
        off = -sx;
        ww += sx;
        sx = 0;
      }
      if (sx + ww > SCR_W) ww = SCR_W - sx;
      if (ww > 0) tft.pushImage(sx, sy, ww, 1, line + off);
    }
  }
  return 1;
}
static bool phDrawJpg(File& f) {
  static uint8_t work[3100];
  JDEC jd;
  PhJpg j{&f, 0, 0};
  JRESULT rc = jd_prepare(&jd, phJpgIn, work, sizeof work, &j);
  if (rc != JDR_OK) {
    phErr = rc == JDR_FMT3 ? "Bu JPEG türü desteklenmiyor (aşamalı)" : "Resim okunamadı (" + String((int)rc) + ")";
    return false;
  }
  uint8_t scale = 0;
  while (scale < 3 && ((jd.width >> scale) > SCR_W || (jd.height >> scale) > SCR_H)) scale++;
  int w = jd.width >> scale, h = jd.height >> scale;
  j.ox = (SCR_W - w) / 2;
  j.oy = (SCR_H - h) / 2;
  rc = jd_decomp(&jd, phJpgOut, scale);
  if (rc != JDR_OK) {
    phErr = "Resim çözülemedi (" + String((int)rc) + ")";
    return false;
  }
  return true;
}
#else
static bool phDrawJpg(File&) {
  for (int y = 20; y < 220; y++) tft.drawFastHLine(30, y, 260, rgbn(40 + y / 3, 60 + y / 4, 90 + y / 5));
  return true;
}
#endif

// --- BMP (24/32 bit ve 16 bit 565, sıkıştırmasız) ---
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static bool phDrawBmp(File& f) {
  uint8_t h[54];
  if (f.read(h, 54) != 54 || h[0] != 'B' || h[1] != 'M') {
    phErr = "BMP okunamadı";
    return false;
  }
  uint32_t off = rd32(h + 10);
  int32_t w = (int32_t)rd32(h + 18), hh = (int32_t)rd32(h + 22);
  int bpp = h[28] | (h[29] << 8), comp = rd32(h + 30);
  bool flip = hh > 0;
  hh = abs(hh);
  if (!(bpp == 24 || bpp == 32 || (bpp == 16 && (comp == 3 || comp == 0))) || (comp != 0 && comp != 3) || w <= 0 || w > 4096) {
    phErr = "Bu BMP türü desteklenmiyor";
    return false;
  }
  int step = 1;
  while (w / step > SCR_W || hh / step > SCR_H) step++;
  int ow = w / step, oh = hh / step, ox = (SCR_W - ow) / 2, oy = (SCR_H - oh) / 2;
  uint32_t rowBytes = ((w * bpp / 8) + 3) & ~3;
  std::unique_ptr<uint8_t[]> row(new (std::nothrow) uint8_t[rowBytes]);
  if (!row) return false;
  std::vector<uint16_t> line(ow);
  for (int y = 0; y < oh; y++) {
    int sy = y * step;
    uint32_t pos = off + (flip ? (hh - 1 - sy) : sy) * rowBytes;
    f.seek(pos);
    f.read(row.get(), rowBytes);
    for (int x = 0; x < ow; x++) {
      int sx = x * step;
      if (bpp == 16) {
        uint16_t v = row[sx * 2] | (row[sx * 2 + 1] << 8);
        line[x] = comp == 3 ? v : (uint16_t)(((v & 0x7C00) << 1) | ((v & 0x3E0) << 1) | (v & 0x1F));  // 555 → 565
      } else {
        const uint8_t* p = &row[sx * (bpp / 8)];
        line[x] = rgbn(p[2], p[1], p[0]);
      }
    }
    tft.pushImage(ox, oy + y, ow, 1, line.data());
  }
  return true;
}

// --- Çizim dosyası (.cizim) ---
static bool phDrawSketch(const String& path) {
  std::vector<HwStroke> st;
  std::vector<int16_t> pts;
  std::vector<uint16_t> cols;
  if (!hwLoadFile(path, st, pts, &cols)) {
    phErr = "Çizim açılamadı";
    return false;
  }
  for (size_t k = 0; k < st.size(); k++) {
    const HwStroke& s = st[k];
    const int16_t* p = &pts[s.start * 2];
    uint16_t col = k < cols.size() ? cols[k] : C_TEXT;
    if (s.n == 1) tft.fillCircle(p[0], p[1], std::max(1, s.w / 2), col);
    for (int i = 1; i < s.n; i++) tft.drawWideLine(p[i * 2 - 2], p[i * 2 - 1], p[i * 2], p[i * 2 + 1], s.w, col, C_BG);
  }
  return true;
}

static void phDrawBar() {
  if (phIdx < 0) return;
  tft.fillRect(0, 0, SCR_W, HDR_H, C_BG);
  hline(0, HDR_H - 1, SCR_W);
  drawIcon(I_BACK, 4, 5, 20, C_TEXT, C_BG);
  text(fit(baseName(phList[phIdx]), F_SM, 220), 30, 15, F_SM, C_TEXT, C_BG, ML_DATUM);
  text(String(phIdx + 1) + "/" + String(phList.size()), 312, 15, F_XS, C_TEXT3, C_BG, MR_DATUM);
}

static void phShow() {
  fillBg(0, 0, SCR_W, SCR_H);
  phErr = "";
  String p = phList[phIdx];
  String e = extOf(p);
  bool ok;
  if (e == "cizim") {
    ok = phDrawSketch(p);
  } else {
    File f = SD.open(p);
    if (!f) {
      phErr = "Dosya açılamadı";
      ok = false;
    } else {
      ok = e == "bmp" ? phDrawBmp(f) : phDrawJpg(f);
      f.close();
    }
  }
  if (!ok) {
    drawIcon(I_PHOTO, 144, 84, 32, C_TEXT3, C_BG);
    text(phErr, 160, 140, F_SM, C_TEXT2, C_BG, MC_DATUM);
  }
  if (phBar || !ok) phDrawBar();
}

static void phDrawList() {
  if (!sdOk) {
    emptyState(HDR_H, SCR_H - HDR_H, I_SD, "SD kart takılı değil", "");
    return;
  }
  if (phList.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_PHOTO, "Resim yok", "JPEG ya da BMP dosyalarını SD karta koy (ör. daxen/foto klasörü).");
    return;
  }
  int n = phList.size();
  phFirst = constrain(phFirst, 0, std::max(0, n - PH_ROWS));
  for (int r = 0; r < PH_ROWS; r++) {
    int i = phFirst + r, y = HDR_H + r * PH_ROW;
    fillBg(0, y, SCR_W, PH_ROW);
    if (i >= n) continue;
    drawIcon(extOf(phList[i]) == "cizim" ? I_PENCIL : I_PHOTO, 12, y + 7, 18, C_TEXT2, C_BG);
    text(fit(baseName(phList[i]), F_SM, 200), 40, y + PH_ROW / 2 + 1, F_SM, C_TEXT, C_BG, ML_DATUM);
    text(fit(dirName(phList[i]), F_XS, 66), 308, y + PH_ROW / 2 + 1, F_XS, C_TEXT3, C_BG, MR_DATUM);
    hline(40, y + PH_ROW - 1, 268);
  }
  fillBg(0, HDR_H + PH_ROW * PH_ROWS, SCR_W, SCR_H - HDR_H - PH_ROW * PH_ROWS);
  scrollBar(HDR_H, PH_ROW * PH_ROWS, phFirst, PH_ROWS, n);
}

static void phDraw(bool full) {
  if (phIdx >= 0) {
    phShow();
    return;
  }
  if (full) header("Fotoğraflar");
  phDrawList();
}

static void phOpen() {
  phList.clear();
  phIdx = -1;
  phBar = false;
  if (!sdOk) return;
  if (phOpenPath.length()) {  // Dosyalar'dan: o klasördeki resimler
    phScan(dirName(phOpenPath), 0);
    std::sort(phList.begin(), phList.end());
    for (size_t i = 0; i < phList.size(); i++)
      if (phList[i] == phOpenPath) phIdx = i;
    phOpenPath = "";
    return;
  }
  const char* dirs[] = {"/daxen/foto", "/daxen/cizim", "/DCIM", "/Resimler", "/Fotograflar", "/Pictures"};
  for (auto d : dirs)
    if (SD.exists(d)) phScan(d, 2);
  phScan("/", 0);
  std::sort(phList.begin(), phList.end(), [](const String& a, const String& b) { return trLower(a) < trLower(b); });
}

static void phTap(int x, int y) {
  if (phIdx >= 0) {
    phBar = !phBar;
    if (phBar) phDrawBar();
    else phShow();
    return;
  }
  int r = (y - HDR_H) / PH_ROW, i = phFirst + r;
  if (y >= HDR_H && r < PH_ROWS && i < (int)phList.size()) {
    phIdx = i;
    phShow();
  }
}

static void phSwipe(int dir) {
  if (phIdx >= 0 && (dir == SW_LEFT || dir == SW_RIGHT)) {
    int n = phList.size();
    phIdx = (phIdx + (dir == SW_LEFT ? 1 : n - 1)) % n;
    phShow();
  } else if (phIdx < 0 && (dir == SW_UP || dir == SW_DOWN)) {
    int n = phList.size();
    phFirst = constrain(phFirst + (dir == SW_UP ? PH_ROWS - 1 : -(PH_ROWS - 1)), 0, std::max(0, n - PH_ROWS));
    phDrawList();
  }
}

static bool phBack() {
  if (phIdx >= 0 && phList.size() > 1) {
    phIdx = -1;
    phDraw(true);
    return true;
  }
  return false;
}

static const App APP_PHOTOS = {"Fotoğraflar", "Fotoğraflar", I_PHOTO, phOpen, phDraw, phTap, nullptr, phSwipe, nullptr, nullptr, phBack, nullptr};
