#pragma once
// Blok Patlat (8×8 blok bulmaca). Sağdaki üç parçadan birini sürükleyip tahtaya bırak; dolan satır ve
// sütunlar patlar. Üç parça da kullanılınca yenileri gelir. Hiçbiri sığmazsa oyun biter.
#include "../../os/shell.h"

static const int BP_N = 8, BP_CELL = 23, BP_X = 9, BP_Y = HDR_H + 9;
static const char* BP_SHAPES[] = {
    "1",        "11",       "1/1",      "111",      "1/1/1",    "1111",     "1/1/1/1",  "11111",    "1/1/1/1/1",
    "11/11",    "111/111/111", "111/111", "11/11/11", "11/1.",  "11/.1",    "1./11",    ".1/11",    "111/1..",
    "111/..1",  "1../111",  "..1/111",  "11/1./1.", "11/.1/.1", "1./1./11", ".1/.1/11", "111/.1.",  ".1./111",
    "1./11/1.", ".1/11/.1", "11./.11",  ".11/11.",  "1./11/.1", ".1/11/1."};
static const int BP_SHAPE_N = sizeof(BP_SHAPES) / sizeof(BP_SHAPES[0]);

struct BpPiece {
  uint8_t shape = 0, w = 0, h = 0, n = 0, color = 1;
  uint8_t cells[9][2];  // (satır, sütun)
  bool used = true;
};
static uint8_t bpBoard[BP_N][BP_N];
static BpPiece bpPieces[3];
static uint32_t bpScore = 0, bpBest = 0;
static int bpCombo = 0, bpMiss = 0;
static bool bpOver = false, bpLoaded = false;
static int bpDrag = -1, bpGhostR = -100, bpGhostC = -100;
static bool bpGhostOk = false;

// Metal tonları: gümüş, çelik mavisi, arduvaz, kurşun, bronz grisi, yosun grisi, eflatun grisi
static uint16_t bpColor(int i) {
  switch (i) {
    case 1: return rgbn(197, 201, 206);
    case 2: return rgbn(120, 146, 165);
    case 3: return rgbn(92, 106, 120);
    case 4: return rgbn(150, 156, 162);
    case 5: return rgbn(165, 150, 132);
    case 6: return rgbn(126, 142, 128);
    default: return rgbn(140, 132, 156);
  }
}

static void bpMakePiece(BpPiece& p, int idx) {
  const char* s = BP_SHAPES[idx];
  p.shape = idx;
  p.n = p.w = 0;
  int r = 0, c = 0;
  for (; *s; s++) {
    if (*s == '/') {
      r++;
      c = 0;
      continue;
    }
    if (*s == '1' && p.n < 9) {
      p.cells[p.n][0] = r;
      p.cells[p.n][1] = c;
      p.n++;
    }
    c++;
    if (c > p.w) p.w = c;
  }
  p.h = r + 1;
  p.used = false;
}

static bool bpFits(const BpPiece& p, int r0, int c0) {
  for (int i = 0; i < p.n; i++) {
    int r = r0 + p.cells[i][0], c = c0 + p.cells[i][1];
    if (r < 0 || c < 0 || r >= BP_N || c >= BP_N || bpBoard[r][c]) return false;
  }
  return true;
}

static bool bpPlaceable(const BpPiece& p) {
  if (p.used) return false;
  for (int r = 0; r + p.h <= BP_N; r++)
    for (int c = 0; c + p.w <= BP_N; c++)
      if (bpFits(p, r, c)) return true;
  return false;
}

static bool bpAnyMove() {
  for (auto& p : bpPieces)
    if (bpPlaceable(p)) return true;
  return false;
}

static void bpNewSet() {
  for (int tries = 0; tries < 12; tries++) {
    for (auto& p : bpPieces) {
      bpMakePiece(p, rnd(BP_SHAPE_N));
      p.color = 1 + rnd(7);
    }
    if (bpAnyMove()) break;
  }
}

static void bpSave() {
  Lock l;
  uint8_t pc[6];
  for (int k = 0; k < 3; k++) {
    pc[k * 2] = bpPieces[k].used ? 255 : bpPieces[k].shape;
    pc[k * 2 + 1] = bpPieces[k].color;
  }
  prefs.begin("bp", false);
  prefs.putBytes("b", bpBoard, sizeof bpBoard);
  prefs.putBytes("p", pc, sizeof pc);
  prefs.putUInt("s", bpScore);
  prefs.putUInt("best", bpBest);
  prefs.putInt("cb", bpCombo);
  prefs.end();
}

static void bpNewGame() {
  memset(bpBoard, 0, sizeof bpBoard);
  bpScore = 0;
  bpCombo = bpMiss = 0;
  bpOver = false;
  bpNewSet();
  bpSave();
}

static void bpLoad() {
  uint8_t pc[6];
  size_t nb, np;
  {
    Lock l;
    prefs.begin("bp", true);
    nb = prefs.getBytes("b", bpBoard, sizeof bpBoard);
    np = prefs.getBytes("p", pc, sizeof pc);
    bpScore = prefs.getUInt("s", 0);
    bpBest = prefs.getUInt("best", 0);
    bpCombo = prefs.getInt("cb", 0);
    prefs.end();
  }
  if (nb != sizeof bpBoard || np != sizeof pc) {
    bpNewGame();
    return;
  }
  bool any = false;
  for (int k = 0; k < 3; k++) {
    if (pc[k * 2] >= BP_SHAPE_N) {
      bpPieces[k].used = true;
      continue;
    }
    bpMakePiece(bpPieces[k], pc[k * 2]);
    bpPieces[k].color = constrain(pc[k * 2 + 1], 1, 7);
    any = true;
  }
  for (auto& row : bpBoard)
    for (auto& v : row)
      if (v > 7) v = 0;
  if (!any) bpNewSet();
  bpOver = !bpAnyMove();
}

static void bpDrawCell(int r, int c, uint16_t col) {
  int x = BP_X + c * BP_CELL, y = BP_Y + r * BP_CELL;
  tft.fillRect(x + 1, y + 1, BP_CELL - 2, BP_CELL - 2, col);
  if (col != C_SURF) {  // metal kabartma: üstte açık, altta koyu çizgi
    tft.drawFastHLine(x + 1, y + 1, BP_CELL - 2, mix565(col, C_TEXT, 0.45f));
    tft.drawFastHLine(x + 1, y + BP_CELL - 2, BP_CELL - 2, mix565(col, C_BG, 0.45f));
  }
}

// Tahta; sürüklenen parça varsa nereye düşeceği ve patlayacak çizgiler gösterilir.
static void bpDrawBoard() {
  bool ghost[BP_N][BP_N] = {};
  bool clearRow[BP_N] = {}, clearCol[BP_N] = {};
  uint16_t pcol = 0;
  if (bpDrag >= 0 && bpGhostOk) {
    const BpPiece& p = bpPieces[bpDrag];
    pcol = bpColor(p.color);
    for (int i = 0; i < p.n; i++) ghost[bpGhostR + p.cells[i][0]][bpGhostC + p.cells[i][1]] = true;
    for (int r = 0; r < BP_N; r++) {
      bool full = true;
      for (int c = 0; c < BP_N; c++)
        if (!bpBoard[r][c] && !ghost[r][c]) full = false;
      clearRow[r] = full;
    }
    for (int c = 0; c < BP_N; c++) {
      bool full = true;
      for (int r = 0; r < BP_N; r++)
        if (!bpBoard[r][c] && !ghost[r][c]) full = false;
      clearCol[c] = full;
    }
  }
  for (int r = 0; r < BP_N; r++)
    for (int c = 0; c < BP_N; c++) {
      uint16_t col;
      if (ghost[r][c]) col = (clearRow[r] || clearCol[c]) ? C_ACC : mix565(C_SURF, pcol, 0.45f);
      else if (bpBoard[r][c]) col = (clearRow[r] || clearCol[c]) ? C_ACC : bpColor(bpBoard[r][c]);
      else col = C_SURF;
      bpDrawCell(r, c, col);
    }
}

static const int BP_SLOT_Y = HDR_H + 78, BP_SLOT_H = 44;
static void bpDrawSlot(int k) {
  int y0 = BP_SLOT_Y + k * BP_SLOT_H;
  fillBg(202, y0, 118, BP_SLOT_H);
  const BpPiece& p = bpPieces[k];
  if (p.used) return;
  const int m = std::min(12, 38 / std::max<int>(p.w, p.h));
  int x0 = 260 - p.w * m / 2, yy = y0 + BP_SLOT_H / 2 - p.h * m / 2;
  uint16_t col = bpColor(p.color);
  if (bpDrag == k) col = mix565(C_BG, col, 0.35f);
  else if (!bpPlaceable(p)) col = mix565(C_BG, col, 0.3f);
  for (int i = 0; i < p.n; i++) tft.fillRect(x0 + p.cells[i][1] * m, yy + p.cells[i][0] * m, m - 1, m - 1, col);
}

static void bpDrawPanel() {
  fillBg(200, HDR_H, 120, 76);
  text("Skor", 206, HDR_H + 8, F_XS, C_TEXT2, C_BG);
  text(String(bpScore), 206, HDR_H + 22, F_LG, C_TEXT, C_BG);
  text("En iyi " + String(bpBest), 206, HDR_H + 56, F_XS, C_TEXT3, C_BG);
  if (bpCombo > 1) text("Kombo ×" + String(bpCombo), 312, HDR_H + 8, F_XS, C_ACC, C_BG, TR_DATUM);
}

static void bpDrawOver() {
  panel(24, 92, 160, 96, C_SURF, C_LINE);
  tft.fillRect(24, 92, 160, 2, C_ACC);
  text("Oyun bitti", 104, 106, F_MD, C_TEXT, C_SURF, TC_DATUM);
  text("Skor " + String(bpScore), 104, 130, F_SM, C_TEXT2, C_SURF, TC_DATUM);
  button(44, 150, 120, 30, "Yeniden", B_PRIMARY, I_REFRESH, C_SURF);
}

static void bpDraw(bool full) {
  if (!bpLoaded) {
    bpLoad();
    bpLoaded = true;
  }
  if (full) {
    header("Blok Patlat");
    headerAction(0, I_REFRESH);
  }
  fillBg(0, HDR_H, 200, SCR_H - HDR_H);
  tft.drawRect(BP_X - 3, BP_Y - 3, BP_N * BP_CELL + 6, BP_N * BP_CELL + 6, C_LINE);
  bpDrawBoard();
  bpDrawPanel();
  for (int k = 0; k < 3; k++) bpDrawSlot(k);
  if (bpOver) bpDrawOver();
}

static void bpClearLines(int& lines) {
  bool row[BP_N], col[BP_N];
  lines = 0;
  for (int r = 0; r < BP_N; r++) {
    row[r] = true;
    for (int c = 0; c < BP_N; c++)
      if (!bpBoard[r][c]) row[r] = false;
    if (row[r]) lines++;
  }
  for (int c = 0; c < BP_N; c++) {
    col[c] = true;
    for (int r = 0; r < BP_N; r++)
      if (!bpBoard[r][c]) col[c] = false;
    if (col[c]) lines++;
  }
  if (!lines) return;
  for (int r = 0; r < BP_N; r++)
    for (int c = 0; c < BP_N; c++)
      if (row[r] || col[c]) bpDrawCell(r, c, C_TEXT);
  delay(110);
  for (int r = 0; r < BP_N; r++)
    for (int c = 0; c < BP_N; c++)
      if (row[r] || col[c]) bpBoard[r][c] = 0;
}

static void bpDown(int x, int y) {
  if (bpOver) {
    if (x >= 44 && x < 164 && y >= 144 && y < 186) {
      bpNewGame();
      bpDraw(false);
    }
    return;
  }
  if (x >= 200 && y >= BP_SLOT_Y && y < BP_SLOT_Y + 3 * BP_SLOT_H) {
    int k = (y - BP_SLOT_Y) / BP_SLOT_H;
    if (k >= 0 && k < 3 && !bpPieces[k].used) {
      bpDrag = k;
      bpGhostR = bpGhostC = -100;
      bpGhostOk = false;
      bpDrawSlot(k);
    }
  }
}

static void bpMove(int x, int y) {
  if (bpDrag < 0) return;
  const BpPiece& p = bpPieces[bpDrag];
  // Parça parmağın biraz üstünde durur ki nereye düştüğü görünsün.
  int ax = x - p.w * BP_CELL / 2, ay = y - p.h * BP_CELL - 14;
  int c0 = (int)lroundf((ax - BP_X) / (float)BP_CELL), r0 = (int)lroundf((ay - BP_Y) / (float)BP_CELL);
  bool ok = bpFits(p, r0, c0);
  if (!ok && !bpGhostOk) return;
  if (r0 == bpGhostR && c0 == bpGhostC && ok == bpGhostOk) return;
  bpGhostR = r0;
  bpGhostC = c0;
  bpGhostOk = ok;
  bpDrawBoard();
}

static void bpUp() {
  if (bpDrag < 0) return;
  int k = bpDrag;
  BpPiece& p = bpPieces[k];
  bool placed = bpGhostOk;
  if (placed) {
    for (int i = 0; i < p.n; i++) bpBoard[bpGhostR + p.cells[i][0]][bpGhostC + p.cells[i][1]] = p.color;
    p.used = true;
  }
  bpDrag = -1;
  bpGhostOk = false;
  bpGhostR = bpGhostC = -100;
  if (!placed) {
    bpDrawBoard();
    bpDrawSlot(k);
    return;
  }
  bpScore += p.n;
  bpDrawBoard();
  int lines = 0;
  bpClearLines(lines);
  if (lines) {
    bpCombo++;
    bpMiss = 0;
    bpScore += 10 * lines * lines + (bpCombo > 1 ? 10 * (bpCombo - 1) * lines : 0);
  } else if (++bpMiss >= 3) {
    bpCombo = 0;
  }
  if (bpPieces[0].used && bpPieces[1].used && bpPieces[2].used) bpNewSet();
  if (bpScore > bpBest) bpBest = bpScore;
  bpOver = !bpAnyMove();
  bpSave();
  bpDraw(false);
}

static void bpTap(int x, int y) {
  if (y < HDR_H && headerHit(x, y) == 0)
    dialog("Yeni oyun?", "Şimdiki oyun silinir.", {"Vazgeç", "Yeni oyun"}, [](int c) {
      if (c == 1) {
        bpNewGame();
        bpDraw(false);
      }
    });
}

static bool bpRaw(uint8_t ev, int x, int y) {
  if (ev == TE_DOWN && y < HDR_H) return false;  // başlık: normal dokunuş
  if (ev == TE_DOWN) bpDown(x, y);
  else if (ev == TE_MOVE) bpMove(x, y);
  else bpUp();
  return true;
}

static void bpClose() {
  if (bpDrag >= 0) bpDrag = -1;
}

static const App APP_BLOKPATLAT = {"Blok Patlat", "Blok Patlat", I_BLOCKS, nullptr, bpDraw, bpTap, nullptr, nullptr, bpRaw, nullptr, nullptr, bpClose};
