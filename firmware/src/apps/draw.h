#pragma once
// Çizim: renkli kalemle serbest çizim. Çizimler SD kartta /daxen/cizim/*.cizim olarak saklanır,
// Fotoğraflar'da da görünür. Geri al, silgi (dokunduğun çizgiyi siler), temizle.
#include "../os/shell.h"
#include <SD.h>

#define DRAW_DIR "/daxen/cizim"
static const uint16_t DRAW_COLS[] = {rgbn(197, 201, 206), rgbn(255, 255, 255), rgbn(120, 146, 165), rgbn(214, 92, 80),  rgbn(226, 150, 72),
                                     rgbn(222, 200, 90),  rgbn(110, 176, 110), rgbn(80, 180, 190),  rgbn(90, 120, 210), rgbn(160, 110, 200)};
static const int DRAW_NC = 10;
static std::vector<HwStroke> drSt;
static std::vector<int16_t> drPts;
static std::vector<uint16_t> drCol;
static std::vector<String> drList;
static String drPath;
static uint8_t drColor = 0, drSize = 3;
static bool drEditing = false, drEraser = false, drPalette = false, drDirty = false;
static int drLx, drLy, drFirst = 0;
static const int DR_ROW = 34, DR_ROWS = 6;

static void drCanvas() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  for (size_t k = 0; k < drSt.size(); k++) {
    const HwStroke& s = drSt[k];
    const int16_t* p = &drPts[s.start * 2];
    if (s.n == 1) tft.fillSmoothCircle(p[0], p[1], std::max(1, s.w / 2), drCol[k], C_BG);
    for (int i = 1; i < s.n; i++) tft.drawWideLine(p[i * 2 - 2], p[i * 2 - 1], p[i * 2], p[i * 2 + 1], s.w, drCol[k], C_BG);
  }
}

static void drToolbar() {
  header("", true, false);
  // renk, kalınlık, silgi, geri al, temizle
  panel(38, 5, 22, 20, DRAW_COLS[drColor], C_LINE);
  int x = 68;
  tft.fillRect(x, 0, 30, HDR_H - 1, C_BG);
  tft.fillCircle(x + 14, 15, std::max(1, drSize / 2 + 1), drEraser ? C_TEXT3 : C_TEXT);
  headerAction(2, I_ERASER, drEraser, false);
  headerAction(1, I_UNDO, false, false);
  headerAction(0, I_TRASH, false, false);
  if (drPalette) {
    panel(4, HDR_H + 2, 312, 36, C_SURF, C_LINE);
    for (int i = 0; i < DRAW_NC; i++) {
      int cx = 10 + i * 30;
      tft.fillRect(cx, HDR_H + 8, 24, 24, DRAW_COLS[i]);
      if (i == drColor) tft.drawRect(cx - 2, HDR_H + 6, 28, 28, C_ACC);
    }
  }
}

static bool drSave() {
  if (!drDirty) return true;
  File f = SD.open(drPath, FILE_WRITE);
  if (!f) return false;
  uint8_t hdr[7] = {'D', 'X', 'E', 'L', 2, (uint8_t)(drSt.size() & 255), (uint8_t)(drSt.size() >> 8)};
  f.write(hdr, 7);
  for (size_t k = 0; k < drSt.size(); k++) {
    const HwStroke& s = drSt[k];
    uint8_t h[5] = {s.w, (uint8_t)(s.n & 255), (uint8_t)(s.n >> 8), (uint8_t)(drCol[k] & 255), (uint8_t)(drCol[k] >> 8)};
    f.write(h, 5);
    f.write((const uint8_t*)&drPts[s.start * 2], s.n * 4);
  }
  f.close();
  drDirty = false;
  return true;
}

static void drLoadList() {
  drList.clear();
  if (!ensureDir(DRAW_DIR)) return;
  File d = SD.open(DRAW_DIR);
  for (File f = d.openNextFile(); f && drList.size() < 200; f = d.openNextFile()) {
    String n = baseName(f.name());
    if (!f.isDirectory() && extOf(n) == "cizim") drList.push_back(n);
    f.close();
  }
  d.close();
  std::sort(drList.begin(), drList.end(), [](const String& a, const String& b) { return a > b; });
}

static void drEdit(const String& path) {
  drPath = path;
  drSt.clear();
  drPts.clear();
  drCol.clear();
  hwLoadFile(path, drSt, drPts, &drCol);
  while (drCol.size() < drSt.size()) drCol.push_back(C_TEXT);
  drEditing = true;
  drDirty = drEraser = drPalette = false;
  drToolbar();
  drCanvas();
}

static void drDraw(bool full) {
  if (drEditing) {
    drCanvas();
    drToolbar();
    return;
  }
  if (full) {
    header("Çizim");
    if (sdOk) headerAction(0, I_PLUS);
  }
  if (!sdOk) {
    emptyState(HDR_H, SCR_H - HDR_H, I_SD, "SD kart gerekli", "Çizimler SD kartta saklanır.");
    return;
  }
  if (drList.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_PENCIL, "Çizim yok", "Sağ üstteki + ile yeni bir sayfa aç.");
    return;
  }
  int n = drList.size();
  drFirst = constrain(drFirst, 0, std::max(0, n - DR_ROWS));
  for (int r = 0; r < DR_ROWS; r++) {
    int i = drFirst + r, y = HDR_H + r * DR_ROW;
    fillBg(0, y, SCR_W, DR_ROW);
    if (i >= n) continue;
    drawIcon(I_PENCIL, 12, y + 8, 18, C_TEXT2, C_BG);
    text(fit(drList[i].substring(0, drList[i].lastIndexOf('.')), F_SM, 260), 40, y + DR_ROW / 2 + 1, F_SM, C_TEXT, C_BG, ML_DATUM);
    hline(40, y + DR_ROW - 1, 268);
  }
  fillBg(0, HDR_H + DR_ROW * DR_ROWS, SCR_W, SCR_H - HDR_H - DR_ROW * DR_ROWS);
}

static void drEraseAt(int x, int y) {
  for (int k = drSt.size() - 1; k >= 0; k--) {
    const HwStroke& s = drSt[k];
    const int16_t* p = &drPts[s.start * 2];
    for (int i = 0; i < s.n; i++)
      if (abs(p[i * 2] - x) < 10 && abs(p[i * 2 + 1] - y) < 10) {
        int start = s.start, n = s.n;
        drPts.erase(drPts.begin() + start * 2, drPts.begin() + (start + n) * 2);
        drSt.erase(drSt.begin() + k);
        drCol.erase(drCol.begin() + k);
        for (auto& o : drSt)
          if (o.start > start) o.start -= n;
        drDirty = true;
        drCanvas();
        return;
      }
  }
}

static bool drRaw(uint8_t ev, int x, int y) {
  if (!drEditing || (ev == TE_DOWN && (y < HDR_H || drPalette))) return false;
  y = std::max(y, HDR_H + 1);
  if (drEraser) {
    if (ev != TE_UP) drEraseAt(x, y);
    return true;
  }
  uint16_t col = DRAW_COLS[drColor];
  if (ev == TE_DOWN) {
    if ((int)drPts.size() / 2 >= HW_MAXPTS) {
      toast("Sayfa doldu");
      return true;
    }
    drSt.push_back(HwStroke{drSize, (uint16_t)(drPts.size() / 2), 1});
    drCol.push_back(col);
    drPts.push_back(x);
    drPts.push_back(y);
    drLx = x;
    drLy = y;
    tft.fillSmoothCircle(x, y, std::max(1, drSize / 2), col, C_BG);
    drDirty = true;
  } else if (ev == TE_MOVE && !drSt.empty()) {
    if (abs(x - drLx) + abs(y - drLy) < 3 || (int)drPts.size() / 2 >= HW_MAXPTS) return true;
    tft.drawWideLine(drLx, drLy, x, y, drSize, col, C_BG);
    drPts.push_back(x);
    drPts.push_back(y);
    drSt.back().n++;
    drLx = x;
    drLy = y;
  }
  return true;
}

static void drTap(int x, int y) {
  if (!drEditing) {
    if (y < HDR_H) {
      if (headerHit(x, y) == 0 && sdOk && ensureDir(DRAW_DIR)) {
        struct tm t;
        String base = localNow(t) ? "Çizim " + String(t.tm_mday) + " " + String(AY_ADI[t.tm_mon]) + " " + fmtHM(t.tm_hour, t.tm_min) : String("Çizim");
        base.replace(":", ".");
        String p = joinPath(DRAW_DIR, base + ".cizim");
        for (int i = 2; SD.exists(p) && i < 100; i++) p = joinPath(DRAW_DIR, base + " (" + String(i) + ").cizim");
        drEdit(p);
      }
      return;
    }
    int r = (y - HDR_H) / DR_ROW, i = drFirst + r;
    if (r < DR_ROWS && i < (int)drList.size()) drEdit(joinPath(DRAW_DIR, drList[i]));
    return;
  }
  if (drPalette) {
    if (y >= HDR_H && y < HDR_H + 40) drColor = constrain((x - 6) / 30, 0, DRAW_NC - 1);
    drPalette = false;
    drEraser = false;
    drCanvas();
    drToolbar();
    return;
  }
  if (y >= HDR_H) return;
  if (x >= 36 && x < 64) {
    drPalette = true;
    drToolbar();
    return;
  }
  if (x >= 64 && x < 100) {
    drSize = drSize == 2 ? 3 : (drSize == 3 ? 6 : (drSize == 6 ? 10 : 2));
    drEraser = false;
    drToolbar();
    return;
  }
  int h = headerHit(x, y, false);
  if (h == 2) {
    drEraser = !drEraser;
    drToolbar();
  } else if (h == 1 && !drSt.empty()) {
    drPts.resize(drSt.back().start * 2);
    drSt.pop_back();
    drCol.pop_back();
    drDirty = true;
    drCanvas();
  } else if (h == 0) {
    dialog("Sayfa temizlensin mi?", "Bu çizimdeki her şey silinir.", {"Vazgeç", "Temizle"}, [](int c) {
      if (c != 1) return;
      drSt.clear();
      drPts.clear();
      drCol.clear();
      drDirty = true;
    });
  }
}

static bool drHold(int x, int y) {
  if (drEditing || y < HDR_H) return false;
  int r = (y - HDR_H) / DR_ROW, i = drFirst + r;
  if (r >= DR_ROWS || i >= (int)drList.size()) return false;
  String p = joinPath(DRAW_DIR, drList[i]);
  dialog("Çizim silinsin mi?", drList[i], {"Vazgeç", "Sil"}, [p](int c) {
    if (c != 1) return;
    SD.remove(p);
    drLoadList();
  });
  return true;
}

static void drCloseEditor() {
  if (!drSave()) toast("Kaydedilemedi");
  if (drSt.empty()) SD.remove(drPath);
  drSt.clear();
  drPts.clear();
  drCol.clear();
  drSt.shrink_to_fit();
  drPts.shrink_to_fit();
  drEditing = false;
}

static bool drBack() {
  if (drEditing) {
    drCloseEditor();
    drLoadList();
    drDraw(true);
    return true;
  }
  return false;
}

static void drOpen() {
  drEditing = false;
  drLoadList();
}
static void drClose() {
  if (drEditing) drCloseEditor();
}

static const App APP_DRAW = {"Çizim", "Çizim", I_PENCIL, drOpen, drDraw, drTap, drHold, nullptr, drRaw, nullptr, drBack, drClose};
