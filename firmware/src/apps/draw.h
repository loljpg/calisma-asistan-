#pragma once
// Çizim: vektör tabanlı çizim uygulaması.
//  - Araçlar: kalem (titreme sabitleyicili), çizgi (45°'ye yaslanır), dikdörtgen, elips, dolu
//    dikdörtgen, dolu elips, yazı, silgi (dokunduğun öğeyi siler), damlalık (öğenin rengini alır).
//  - 20 renk, 1-20 px kalınlık; geri al / yinele (40 adım, silme ve temizleme de geri alınır).
//  - Ayna (dikey eksende simetri), kılavuz (noktalı ızgara; şekiller ızgaraya yaslanır), kağıt
//    rengi (koyu, siyah, beyaz).
//  - Resim olarak kaydetme: /daxen/foto/*.bmp (Fotoğraflar'da görünür, Aktarım'dan indirilir).
//  - Liste küçük resimli; basılı tut: yeniden adlandır, kopyala, resim olarak kaydet, sil.
// Çizimler SD kartta /daxen/cizim/*.cizim (DXEL sürüm 3; sürüm 1-2 dosyalar da açılır),
// Fotoğraflar'da da görünür. Boşta 20 sn kalınca kendiliğinden kaydedilir.
// Bellek: çizim nokta listesi olarak tutulur (nokta başına 4 bayt, sayfada en çok 5000 nokta);
// ekran görüntüsü tutulmaz. Kalem çizgisi bırakılınca seyreltilir (görünmeyen ara noktalar atılır).
// Liste 1 KB'lık adımlarla büyür; her adımda 18 KB taban denetlenir (docs/bellek.md).
#include "../os/shell.h"
#include "../core/drawcalc.h"
#include <SD.h>
#include <climits>

#define DRAW_DIR "/daxen/cizim"
#define DR_EXPORT_DIR "/daxen/foto"
static const int DR_MAXPTS = 5000, DR_CY = HDR_H, DR_GRID = 20, DR_OPS_MAX = 40;
static const uint16_t DR_COLS[] = {
    rgbn(245, 245, 242), rgbn(197, 201, 206), rgbn(130, 136, 142), rgbn(62, 66, 70),   rgbn(0, 0, 0),
    rgbn(128, 88, 62),   rgbn(214, 92, 80),   rgbn(226, 150, 72),  rgbn(222, 200, 90), rgbn(160, 200, 90),
    rgbn(110, 176, 110), rgbn(70, 150, 120),  rgbn(80, 180, 190),  rgbn(120, 170, 225), rgbn(90, 120, 210),
    rgbn(110, 90, 190),  rgbn(160, 110, 200), rgbn(220, 120, 170), rgbn(200, 70, 140), rgbn(120, 146, 165)};
static const int DR_NC = 20;
static const uint16_t DR_PAPERS[] = {C_BG, 0x0000, rgbn(236, 234, 228)};
static const char* const DR_PAPER_NAME[] = {"koyu", "siyah", "beyaz"};

enum DrKind : uint8_t { DK_PEN, DK_LINE, DK_RECT, DK_OVAL, DK_RECTF, DK_OVALF, DK_TEXT, DK_N };
enum DrTool : uint8_t { DT_PEN, DT_LINE, DT_RECT, DT_OVAL, DT_RECTF, DT_OVALF, DT_TEXT, DT_ERASER, DT_PICK, DT_N };
static const uint8_t DT_ICON[DT_N] = {I_PENCIL, I_LINE, I_RECT, I_OVAL, I_RECTF, I_OVALF, I_TEXT, I_ERASER, I_DROPPER};
static const char* const DT_NAME[DT_N] = {"Kalem", "Çizgi", "Dikdörtgen", "Elips", "Dolu dikdörtgen", "Dolu elips", "Yazı",
                                          "Silgi", "Damlalık"};

struct DrItem {
  uint8_t kind, w;  // yazıda w: yazı tipi (0 küçük, 1 orta, 2 büyük)
  uint16_t col, start, n;
  bool hid;  // silindi ya da geri alındı (kayda yazılmaz)
};
#define DRF_GRID 1
struct DrDoc {
  std::vector<DrItem> it;
  std::vector<int16_t> pts;  // x, y çiftleri (ekran koordinatı)
  uint16_t paper = C_BG;
  uint8_t flags = 0;
};
struct DrOp {  // geri alma adımı: eklenen ya da silinen öğeler
  bool add;
  std::vector<uint16_t> ids;
};

static DrDoc drDoc;
static std::vector<DrOp> drOps;
static int drOpPos = 0;  // bundan sonrakiler yinelenebilir
static std::vector<String> drList;
static String drPath;
static uint8_t drColor = 1, drSize = 3, drTool = DT_PEN, drDrawTool = DT_PEN, drPanel = 0;  // panel: 1 araç, 2 renk
static bool drEditing = false, drDirty = false, drMirror = false, drStab = true, drDown = false, drSizeDrag = false;
static int drPage = 0, drLx, drLy, drAx, drAy, drBx, drBy, drEraseOp = -1;
static float drFx, drFy;
static uint32_t drMoveAt = 0, drEditAt = 0;
static int drPrev[4];
static bool drHasPrev = false;
static const int DRP_TOOLS_H = 44, DRP_BRUSH_H = 98;

static Font drFont(uint8_t i) { return i == 0 ? F_XS : (i == 1 ? F_SM : F_MD); }
static uint8_t drFontFor(uint8_t size) { return size <= 4 ? 0 : (size <= 9 ? 1 : 2); }
static bool drMemOk(size_t bytes) { return sysHeapFree() >= bytes + MEM_FLOOR + 2048 && sysHeapMaxAlloc() >= bytes + 256; }
static int drMirrorX(int x) { return SCR_W - 1 - x; }

// --- Çizdirme (ekran, ara bellek ya da küçük resim; s ölçek, ox/oy kayma) ---
// Kalın çoklu çizgi iki geçişte: önce kenarı yumuşatılmış parçalar, sonra her parçanın içi düz renkle.
// Kitaplık yumuşak kenarı arka plan rengiyle karıştırır; parçaların uçları birbirinin üstüne
// binince eklem yerlerinde koyu halkalar kalırdı, ikinci geçiş onları örter.
template <class F>
static void drPoly(TFT_eSPI& g, int n, F pt, float w, uint16_t c, uint16_t bg) {
  float ax, ay, bx, by;
  for (int pass = 0; pass < (w >= 2.5f ? 2 : 1); pass++) {
    pt(0, ax, ay);
    for (int i = 1; i < n; i++) {
      pt(i, bx, by);
      if (pass == 0) g.drawWideLine(ax, ay, bx, by, w, c, bg);
      else g.drawWideLine(ax, ay, bx, by, w - 1.5f, c, c);
      ax = bx, ay = by;
    }
  }
}

// Hedef ekran ya da ara bellek: ara bellek de TFT_eSPI'dir (çizim işlevleri sanal piksel işlemleriyle
// ona yazar); yalnızca yazı ara belleğin kendi yoluyla çizilir (sp).
static void drItemDraw(TFT_eSPI& g, TFT_eSprite* sp, const DrItem& it, const int16_t* p, float s, float ox, float oy, uint16_t bg) {
  auto X = [&](int v) { return ox + v * s; };
  auto Y = [&](int v) { return oy + v * s; };
  float w = std::max(1.f, it.w * s);
  uint16_t c = it.col;
  switch (it.kind) {
    case DK_PEN:
      if (it.n == 1) {
        g.fillSmoothCircle(lroundf(X(p[0])), lroundf(Y(p[1])), std::max(1, (int)lroundf(w / 2)), c, bg);
        break;
      }
      drPoly(g, it.n, [&](int i, float& x, float& y) { x = X(p[2 * i]), y = Y(p[2 * i + 1]); }, w, c, bg);
      break;
    case DK_LINE:
      g.drawWideLine(X(p[0]), Y(p[1]), X(p[2]), Y(p[3]), w, c, bg);
      break;
    case DK_RECT: {
      const float xs[5] = {X(p[0]), X(p[2]), X(p[2]), X(p[0]), X(p[0])}, ys[5] = {Y(p[1]), Y(p[1]), Y(p[3]), Y(p[3]), Y(p[1])};
      drPoly(g, 5, [&](int i, float& x, float& y) { x = xs[i], y = ys[i]; }, w, c, bg);
      break;
    }
    case DK_RECTF: {
      int x0 = lroundf(std::min(X(p[0]), X(p[2]))), x1 = lroundf(std::max(X(p[0]), X(p[2])));
      int y0 = lroundf(std::min(Y(p[1]), Y(p[3]))), y1 = lroundf(std::max(Y(p[1]), Y(p[3])));
      g.fillRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, c);
      break;
    }
    case DK_OVAL:
    case DK_OVALF: {
      float cx = (X(p[0]) + X(p[2])) / 2, cy = (Y(p[1]) + Y(p[3])) / 2;
      float rx = fabsf(X(p[2]) - X(p[0])) / 2, ry = fabsf(Y(p[3]) - Y(p[1])) / 2;
      if (it.kind == DK_OVALF) {
        for (int y = (int)ceilf(cy - ry); y <= (int)floorf(cy + ry); y++) {
          float t = ry > 0 ? (y - cy) / ry : 0, dx = rx * sqrtf(std::max(0.f, 1 - t * t));
          int xa = lroundf(cx - dx), xb = lroundf(cx + dx);
          g.fillRect(xa, y, xb - xa + 1, 1, c);
        }
      } else {
        int n = drOvalSegs(rx, ry);
        drPoly(g, n + 1, [&](int i, float& x, float& y) { drOvalPt(cx, cy, rx, ry, i, n, x, y); }, w, c, bg);
      }
      break;
    }
    case DK_TEXT: {
      char t[DR_TEXT_MAX + 1];
      if (!drTextUnpack(p, it.n, t)) break;
      Font f = drFont(it.w);
      if (s > 0.9f) {
        if (sp) fontDraw(*sp, f, String(t), lroundf(X(p[0])), lroundf(Y(p[1])), c, bg, TL_DATUM);
        else fontDraw(g, f, String(t), lroundf(X(p[0])), lroundf(Y(p[1])), c, bg, TL_DATUM);
      } else {  // küçük resim: yazının yerinde ince bir şerit
        int h = fontHeight(f);
        g.fillRect(lroundf(X(p[0])), lroundf(Y(p[1]) + h * s * 0.3f), std::max(2, (int)lroundf(fontWidth(f, t) * s)),
                   std::max(1, (int)lroundf(h * s * 0.4f)), c);
      }
      break;
    }
  }
}

// Öğenin kapladığı alan (ekran koordinatı, sağ ve alt dahil değil)
static void drBox(const DrItem& it, const int16_t* p, int b[4]) {
  if (it.kind == DK_TEXT) {
    char t[DR_TEXT_MAX + 1];
    drTextUnpack(p, it.n, t);
    b[0] = p[0], b[1] = p[1], b[2] = p[0] + fontWidth(drFont(it.w), t) + 2, b[3] = p[1] + fontHeight(drFont(it.w)) + 2;
    return;
  }
  int x0 = INT16_MAX, y0 = INT16_MAX, x1 = INT16_MIN, y1 = INT16_MIN;
  for (int i = 0; i < it.n; i++) {
    x0 = std::min<int>(x0, p[2 * i]), x1 = std::max<int>(x1, p[2 * i]);
    y0 = std::min<int>(y0, p[2 * i + 1]), y1 = std::max<int>(y1, p[2 * i + 1]);
  }
  int m = it.w / 2 + 2;
  b[0] = x0 - m, b[1] = y0 - m, b[2] = x1 + m + 1, b[3] = y1 + m + 1;
}
static const int16_t* drP(const DrDoc& d, const DrItem& it) { return &d.pts[it.start * 2]; }
static void drUnion(int a[4], const int b[4]) {
  a[0] = std::min(a[0], b[0]), a[1] = std::min(a[1], b[1]), a[2] = std::max(a[2], b[2]), a[3] = std::max(a[3], b[3]);
}

// Bir dikdörtgeni baştan çizer: kağıt, kılavuz, o alana değen öğeler (ekranın kırpma penceresiyle)
static void drPaint(int x0, int y0, int x1, int y1) {
  x0 = std::max(x0, 0), y0 = std::max(y0, DR_CY), x1 = std::min(x1, SCR_W), y1 = std::min(y1, SCR_H);
  if (x1 <= x0 || y1 <= y0) return;
  tft.setViewport(x0, y0, x1 - x0, y1 - y0, false);
  const uint16_t paper = drDoc.paper;
  tft.fillRect(x0, y0, x1 - x0, y1 - y0, paper);
  bool light = paper == DR_PAPERS[2];
  if (drDoc.flags & DRF_GRID) {
    uint16_t dot = light ? rgbn(196, 196, 190) : grayn(0x2A);
    for (int gy = DR_CY + DR_GRID * ((y0 - DR_CY + DR_GRID - 1) / DR_GRID); gy < y1; gy += DR_GRID)
      for (int gx = DR_GRID * ((x0 + DR_GRID - 1) / DR_GRID); gx < x1; gx += DR_GRID) tft.drawPixel(gx, gy, dot);
  }
  if (drMirror && x0 <= SCR_W / 2 && x1 > SCR_W / 2)
    for (int y = y0; y < y1; y++)
      if ((y / 4) % 2 == 0) tft.drawPixel(SCR_W / 2, y, light ? rgbn(170, 180, 190) : C_ACC);
  for (auto& it : drDoc.it) {
    if (it.hid) continue;
    int b[4];
    drBox(it, drP(drDoc, it), b);
    if (b[2] <= x0 || b[0] >= x1 || b[3] <= y0 || b[1] >= y1) continue;
    drItemDraw(tft, nullptr, it, drP(drDoc, it), 1, 0, 0, paper);
  }
  tft.resetViewport();
}
static void drPaintBox(const int b[4]) { drPaint(b[0], b[1], b[2], b[3]); }
static void drPaintAll() { drPaint(0, DR_CY, SCR_W, SCR_H); }

// Tuvalde canlı çizim (başlığa taşmasın)
struct DrClip {
  DrClip() { tft.setViewport(0, DR_CY, SCR_W, SCR_H - DR_CY, false); }
  ~DrClip() { tft.resetViewport(); }
};

// --- Başlık ve paneller ---
static void drAct(int k, uint8_t icon, bool en) {
  int x = hdrActX(k, false);
  tft.fillRect(x, 0, 30, HDR_H - 1, C_BG);
  drawIcon(icon, x + 5, 5, 20, en ? C_TEXT2 : C_LINE, C_BG);
}
static void drActs() {
  drAct(2, I_UNDO, drOpPos > 0);
  drAct(1, I_REDO, drOpPos < (int)drOps.size());
  drAct(0, I_MORE, true);
}
static void drSizeDot() {
  tft.fillRect(98, 0, 34, HDR_H - 1, C_BG);
  tft.fillCircle(115, 14, constrain(drSize / 2, 1, 9), drTool == DT_ERASER ? C_TEXT3 : C_TEXT);
}
static void drDrawSizeRow() {
  int y = DR_CY + 64;
  tft.fillRect(4, y, 312, 32, C_SURF);
  text("Kalınlık", 10, y + 14, F_XS, C_TEXT2, C_SURF, ML_DATUM);
  slider(70, y + 2, 180, (drSize - 1) * 100 / 19, C_SURF);
  tft.fillCircle(276, y + 14, constrain(drSize / 2, 1, 10), DR_COLS[drColor]);
  text(String(drSize), 310, y + 14, F_XS, C_TEXT2, C_SURF, MR_DATUM);
}
static void drDrawPanel() {
  if (drPanel == 1) {
    panel(2, DR_CY + 2, 316, DRP_TOOLS_H, C_SURF, C_LINE);
    for (int i = 0; i < DT_N; i++) {
      int x = 7 + i * 34;
      bool on = i == drTool;
      if (on) panel(x, DR_CY + 6, 32, 36, C_ACCBG, C_ACC);
      drawIcon(DT_ICON[i], x + 6, DR_CY + 14, 20, on ? C_ACC : C_TEXT, on ? C_ACCBG : C_SURF);
    }
  } else if (drPanel == 2) {
    panel(2, DR_CY + 2, 316, DRP_BRUSH_H, C_SURF, C_LINE);
    for (int i = 0; i < DR_NC; i++) {
      int x = 9 + (i % 10) * 31, y = DR_CY + 9 + (i / 10) * 28;
      tft.drawRect(x - 2, y - 2, 30, 26, i == drColor ? C_ACC : C_SURF);
      tft.fillRect(x, y, 26, 22, DR_COLS[i]);
      if (DR_COLS[i] == 0) tft.drawRect(x, y, 26, 22, C_LINE);  // siyah görünsün
    }
    drDrawSizeRow();
  }
}
static void drToolbar() {
  header("", true, false);
  drawIcon(DT_ICON[drTool], 39, 5, 20, drPanel == 1 ? C_ACC : C_TEXT, C_BG);
  panel(70, 5, 24, 20, DR_COLS[drColor], drPanel == 2 ? C_ACC : C_LINE);
  drSizeDot();
  if (drMirror) drawIcon(I_MIRROR, 142, 7, 16, C_ACC, C_BG);
  if (drDoc.flags & DRF_GRID) drawIcon(I_GRID, 162, 7, 16, C_ACC, C_BG);
  drActs();
  if (drPanel) drDrawPanel();
}
static void drClosePanel() {
  int h = drPanel == 1 ? DRP_TOOLS_H : DRP_BRUSH_H;
  drPanel = 0;
  drPaint(0, DR_CY, SCR_W, DR_CY + h + 4);
  drToolbar();
}

// --- Dosya ---
// Sürüm 3: "DXEL", 3, kağıt (2), bayraklar (1), öğe sayısı (2); öğe: tür, kalınlık, renk (2), nokta
// sayısı (2), noktalar (x, y; 2'şer bayt). Sürüm 1-2 (eski): yalnızca kalem çizgileri.
static bool drLoad(const String& path, DrDoc& d) {
  d = DrDoc();
  File f = SD.open(path);
  if (!f) return false;
  uint8_t h[5];
  if (f.read(h, 5) != 5 || memcmp(h, "DXEL", 4)) {
    f.close();
    return false;
  }
  uint8_t ver = h[4], e[3], c[2];
  if (ver >= 3) {
    if (f.read(e, 3) != 3) {
      f.close();
      return false;
    }
    d.paper = e[0] | e[1] << 8;
    d.flags = e[2];
  }
  f.read(c, 2);
  int n = c[0] | c[1] << 8;
  size_t est = std::min<size_t>((size_t)DR_MAXPTS * 2, f.size() / 2);
  if (!drMemOk(est * 2 + n * sizeof(DrItem))) {
    f.close();
    return false;
  }
  d.pts.reserve(est);
  d.it.reserve(n);
  for (int i = 0; i < n; i++) {
    DrItem it{DK_PEN, 3, C_TEXT, (uint16_t)(d.pts.size() / 2), 0, false};
    uint8_t b[6];
    if (ver >= 3) {
      if (f.read(b, 6) != 6) break;
      it.kind = b[0], it.w = b[1], it.col = b[2] | b[3] << 8, it.n = b[4] | b[5] << 8;
    } else {
      if (f.read(b, 3) != 3) break;
      it.w = b[0], it.n = b[1] | b[2] << 8;
      if (ver >= 2 && f.read(c, 2) == 2) it.col = c[0] | c[1] << 8;
    }
    if (it.kind >= DK_N || it.n == 0 || (int)(d.pts.size() / 2) + it.n > DR_MAXPTS) break;
    size_t at = d.pts.size();
    d.pts.resize(at + it.n * 2);
    if (f.read((uint8_t*)&d.pts[at], it.n * 4) != (size_t)it.n * 4) {
      d.pts.resize(at);
      break;
    }
    bool bad = it.kind != DK_PEN && it.kind != DK_TEXT && it.n != 2;  // şekil: iki köşe
    if (it.kind == DK_TEXT) {
      int len = it.n >= 2 ? d.pts[at + 2] : -1;
      bad = len < 0 || len > DR_TEXT_MAX || drTextPts(len) > it.n;
    }
    if (bad) {
      d.pts.resize(at);
      continue;
    }
    d.it.push_back(it);
  }
  f.close();
  return true;
}

// Görünen öğeleri yazar (geçici dosyaya, sonra adını değiştirir: yarım kayıt eskisini bozmaz)
static bool drWrite(const String& path, const DrDoc& d) {
  String tmp = path + ".tmp";
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  uint16_t n = 0;
  for (auto& it : d.it) n += !it.hid;
  uint8_t h[10] = {'D', 'X', 'E', 'L', 3, (uint8_t)(d.paper & 255), (uint8_t)(d.paper >> 8), d.flags, (uint8_t)(n & 255),
                   (uint8_t)(n >> 8)};
  bool ok = f.write(h, 10) == 10;
  for (auto& it : d.it) {
    if (it.hid || !ok) continue;
    uint8_t b[6] = {it.kind, it.w, (uint8_t)(it.col & 255), (uint8_t)(it.col >> 8), (uint8_t)(it.n & 255), (uint8_t)(it.n >> 8)};
    ok = f.write(b, 6) == 6 && f.write((const uint8_t*)drP(d, it), it.n * 4) == (size_t)it.n * 4;
  }
  f.close();
  if (!ok) {
    SD.remove(tmp);
    return false;
  }
  SD.remove(path);
  return SD.rename(tmp, path);
}

static bool drSave() {
  if (!drDirty) return true;
  if (!drWrite(drPath, drDoc)) return false;
  drDirty = false;
  return true;
}

// Silinmiş ve geri alınmış öğeleri bellekten atar (geri alma geçmişi de silinir)
static void drCompact() {
  auto& P = drDoc.pts;
  size_t w = 0, k = 0;
  for (size_t i = 0; i < drDoc.it.size(); i++) {
    DrItem c = drDoc.it[i];
    if (c.hid) continue;
    if (c.start * 2 != w) memmove(&P[w], &P[c.start * 2], c.n * 4);
    c.start = w / 2;
    drDoc.it[k++] = c;
    w += c.n * 2;
  }
  P.resize(w);
  drDoc.it.resize(k);
  drOps.clear();
  drOpPos = 0;
}

// addPts nokta ve iki öğe için yer: sayfa sınırı ve 18 KB bellek tabanı
static bool drRoom(int addPts) {
  auto& P = drDoc.pts;
  auto& I = drDoc.it;
  if ((int)P.size() / 2 + addPts > DR_MAXPTS) {
    drCompact();
    if (drEditing) drActs();
    if ((int)P.size() / 2 + addPts > DR_MAXPTS) {
      toast("Sayfa doldu");
      return false;
    }
  }
  if (P.capacity() < P.size() + addPts * 2) {
    size_t cap = std::min<size_t>((size_t)DR_MAXPTS * 2, P.size() + addPts * 2 + 512);  // 1 KB'lık adımlarla
    if (!drMemOk(cap * 2)) {
      toast("Bellek yetmiyor: çizimi kaydedip kapat");
      return false;
    }
    P.reserve(cap);
  }
  if (I.capacity() < I.size() + 2) {
    size_t cap = I.size() + 32;
    if (!drMemOk(cap * sizeof(DrItem))) {
      toast("Bellek yetmiyor: çizimi kaydedip kapat");
      return false;
    }
    I.reserve(cap);
  }
  return true;
}

static uint16_t drAdd(uint8_t kind, uint8_t w, uint16_t col, const int16_t* p, int n) {
  DrItem it{kind, w, col, (uint16_t)(drDoc.pts.size() / 2), (uint16_t)n, false};
  drDoc.pts.insert(drDoc.pts.end(), p, p + n * 2);
  drDoc.it.push_back(it);
  drDirty = true;
  drEditAt = millis();
  return drDoc.it.size() - 1;
}

// --- Geri al / yinele ---
static void drPushOp(bool add, std::vector<uint16_t> ids) {
  drOps.resize(drOpPos);  // yinelenebilecekler atılır (geri alınmış eklemeler gizli kalır, kayda girmez)
  drOps.push_back(DrOp{add, std::move(ids)});
  if ((int)drOps.size() > DR_OPS_MAX) drOps.erase(drOps.begin());
  drOpPos = drOps.size();
  drDirty = true;
  drEditAt = millis();
  if (drEditing) drActs();
}
static void drApply(const DrOp& op, bool hide) {
  int b[4] = {SCR_W, SCR_H, 0, 0};
  for (uint16_t id : op.ids) {
    if (id >= drDoc.it.size()) continue;
    DrItem& it = drDoc.it[id];
    it.hid = hide;
    int bb[4];
    drBox(it, drP(drDoc, it), bb);
    drUnion(b, bb);
  }
  drDirty = true;
  drEditAt = millis();
  drPaintBox(b);
  drActs();
}
static void drUndo() {
  if (drOpPos <= 0) return;
  const DrOp& op = drOps[--drOpPos];
  drApply(op, op.add);
}
static void drRedo() {
  if (drOpPos >= (int)drOps.size()) return;
  const DrOp& op = drOps[drOpPos++];
  drApply(op, !op.add);
}

// --- Seçme: noktaya en üstteki görünen öğe ---
static int drHit(int x, int y, int tol) {
  for (int k = drDoc.it.size() - 1; k >= 0; k--) {
    const DrItem& it = drDoc.it[k];
    if (it.hid) continue;
    const int16_t* p = drP(drDoc, it);
    int b[4];
    drBox(it, p, b);
    if (x < b[0] - tol || x >= b[2] + tol || y < b[1] - tol || y >= b[3] + tol) continue;
    float r = it.w / 2.f + tol;
    bool hit = false;
    switch (it.kind) {
      case DK_PEN:
        if (it.n == 1) hit = drSegDist(x, y, p[0], p[1], p[0], p[1]) <= r;
        for (int i = 1; i < it.n && !hit; i++) hit = drSegDist(x, y, p[2 * i - 2], p[2 * i - 1], p[2 * i], p[2 * i + 1]) <= r;
        break;
      case DK_LINE: hit = drSegDist(x, y, p[0], p[1], p[2], p[3]) <= r; break;
      case DK_RECT:
        hit = drSegDist(x, y, p[0], p[1], p[2], p[1]) <= r || drSegDist(x, y, p[2], p[1], p[2], p[3]) <= r ||
              drSegDist(x, y, p[2], p[3], p[0], p[3]) <= r || drSegDist(x, y, p[0], p[3], p[0], p[1]) <= r;
        break;
      case DK_OVAL:
      case DK_OVALF: {
        float cx = (p[0] + p[2]) / 2.f, cy = (p[1] + p[3]) / 2.f, rx = abs(p[2] - p[0]) / 2.f, ry = abs(p[3] - p[1]) / 2.f;
        float ex = (x - cx) / (rx + r), ey = (y - cy) / (ry + r), e = ex * ex + ey * ey;
        float ix = rx > r ? (x - cx) / (rx - r) : 9, iy = ry > r ? (y - cy) / (ry - r) : 9;
        hit = e <= 1 && (it.kind == DK_OVALF || ix * ix + iy * iy >= 1);
        break;
      }
      default: hit = true; break;  // dolu dikdörtgen, yazı: alanın içi
    }
    if (hit) return k;
  }
  return -1;
}

// --- Şekil önizlemesi ---
static uint8_t drKindOf(uint8_t tool) {
  static const uint8_t K[] = {DK_PEN, DK_LINE, DK_RECT, DK_OVAL, DK_RECTF, DK_OVALF};
  return tool < 6 ? K[tool] : DK_PEN;
}
static void drShapeEnd(int x, int y, int& bx, int& by) {
  bx = x, by = y;
  if (drDoc.flags & DRF_GRID) bx = drGridSnap(bx, DR_GRID, 0), by = drGridSnap(by, DR_GRID, DR_CY);
  else if (drTool == DT_LINE) drSnap45(drAx, drAy, bx, by);
  bx = constrain(bx, 0, SCR_W - 1), by = constrain(by, DR_CY + 1, SCR_H - 1);
}
static void drShapeBox(int bx, int by, int b[4]) {
  int16_t p[4] = {(int16_t)drAx, (int16_t)drAy, (int16_t)bx, (int16_t)by};
  DrItem it{drKindOf(drTool), drSize, 0, 0, 2, false};
  drBox(it, p, b);
  if (drMirror) {
    int16_t q[4] = {(int16_t)drMirrorX(drAx), (int16_t)drAy, (int16_t)drMirrorX(bx), (int16_t)by};
    int bb[4];
    drBox(it, q, bb);
    drUnion(b, bb);
  }
}
static void drShapeDraw(int bx, int by) {
  DrClip c;
  DrItem it{drKindOf(drTool), drSize, DR_COLS[drColor], 0, 2, false};
  int16_t p[4] = {(int16_t)drAx, (int16_t)drAy, (int16_t)bx, (int16_t)by};
  drItemDraw(tft, nullptr, it, p, 1, 0, 0, drDoc.paper);
  if (drMirror) {
    int16_t q[4] = {(int16_t)drMirrorX(drAx), (int16_t)drAy, (int16_t)drMirrorX(bx), (int16_t)by};
    drItemDraw(tft, nullptr, it, q, 1, 0, 0, drDoc.paper);
  }
}

// --- Kalem ---
// Canlı çizimde yeni parça; kalınsa önceki parçayla birlikte içleri düz renkle (drPoly gibi)
static void drPenSeg(int ax, int ay, int bx, int by) {
  DrClip c;
  uint16_t col = DR_COLS[drColor];
  const DrItem& it = drDoc.it.back();
  bool prev = it.n >= 2;
  const int16_t* p = drP(drDoc, it) + (it.n - 2) * 2;  // önceki parçanın başı (ax, ay'den önceki nokta)
  for (int m = 0; m < (drMirror ? 2 : 1); m++) {
    auto X = [&](int x) { return m ? drMirrorX(x) : x; };
    tft.drawWideLine(X(ax), ay, X(bx), by, drSize, col, drDoc.paper);
    if (drSize >= 3) {
      if (prev) tft.drawWideLine(X(p[0]), p[1], X(ax), ay, drSize - 1.5f, col, col);
      tft.drawWideLine(X(ax), ay, X(bx), by, drSize - 1.5f, col, col);
    }
  }
}
static void drPenDown(int x, int y) {
  if (!drRoom(1)) return;
  int16_t p[2] = {(int16_t)x, (int16_t)y};
  drAdd(DK_PEN, drSize, DR_COLS[drColor], p, 1);
  drFx = x, drFy = y, drLx = x, drLy = y;
  DrClip c;
  int r = std::max(1, drSize / 2);
  tft.fillSmoothCircle(x, y, r, DR_COLS[drColor], drDoc.paper);
  if (drMirror) tft.fillSmoothCircle(drMirrorX(x), y, r, DR_COLS[drColor], drDoc.paper);
  drDown = true;
}
static void drPenPoint(int nx, int ny) {
  if (!drRoom(1)) {
    drDown = false;
    return;
  }
  drPenSeg(drLx, drLy, nx, ny);
  drDoc.pts.push_back(nx);
  drDoc.pts.push_back(ny);
  drDoc.it.back().n++;
  drLx = nx, drLy = ny;
}
static void drPenUp(int x, int y) {
  if (drStab && abs(x - drLx) + abs(y - drLy) >= 2) drPenPoint(x, y);  // sabitleyicinin geride bıraktığı uç
  uint16_t k = drDoc.it.size() - 1;
  DrItem& it = drDoc.it[k];
  it.n = drSimplify(&drDoc.pts[it.start * 2], it.n, 0.6f);
  drDoc.pts.resize((it.start + it.n) * 2);
  std::vector<uint16_t> ids{k};
  if (drMirror) {
    int n = drDoc.it[k].n;
    if (drRoom(n)) {
      std::vector<int16_t> q(drDoc.pts.begin() + drDoc.it[k].start * 2, drDoc.pts.begin() + (drDoc.it[k].start + n) * 2);
      for (int i = 0; i < n; i++) q[2 * i] = drMirrorX(q[2 * i]);
      ids.push_back(drAdd(DK_PEN, drDoc.it[k].w, drDoc.it[k].col, q.data(), n));
    } else {
      int b[4];
      drBox(drDoc.it[k], drP(drDoc, drDoc.it[k]), b);
      drPaint(drMirrorX(b[2]), b[1], drMirrorX(b[0]) + 1, b[3]);  // eklenemeyen aynayı sil
    }
  }
  drPushOp(true, ids);
}

// --- Dokunma ---
static void drSetSize(int x) {
  drSize = constrain(1 + (x - 70) * 19 / 180, 1, 20);
  drDrawSizeRow();
  drSizeDot();
}

static bool drRaw(uint8_t ev, int x, int y) {
  if (!drEditing) return false;
  if (ev == TE_DOWN) {
    if (y < DR_CY) return false;
    if (drPanel == 2 && y >= DR_CY + 62 && y < DR_CY + DRP_BRUSH_H) {
      drSizeDrag = true;
      drSetSize(x);
      return true;
    }
    if (drPanel || drTool == DT_TEXT || drTool == DT_PICK) return false;  // dokunma (drTap)
  } else if (drSizeDrag) {
    if (ev == TE_MOVE) drSetSize(x);
    else drSizeDrag = false;
    return true;
  }
  x = constrain(x, 0, SCR_W - 1);
  y = constrain(y, DR_CY + 1, SCR_H - 1);
  if (drTool == DT_ERASER) {
    if (ev == TE_DOWN) drEraseOp = -1;
    if (ev == TE_UP) return true;
    int k = drHit(x, y, 6);
    if (k >= 0) {
      drDoc.it[k].hid = true;
      if (drEraseOp < 0) {
        drPushOp(false, {(uint16_t)k});
        drEraseOp = drOpPos - 1;
      } else {
        drOps[drEraseOp].ids.push_back(k);
      }
      int b[4];
      drBox(drDoc.it[k], drP(drDoc, drDoc.it[k]), b);
      drPaintBox(b);
    }
    return true;
  }
  if (drTool == DT_PEN) {
    if (ev == TE_DOWN) {
      drPenDown(x, y);
    } else if (drDown) {
      if (ev == TE_MOVE) {
        float k = drStab ? 0.45f : 1.f;  // sabitleyici: parmağın titremesi süzülür
        drFx += (x - drFx) * k, drFy += (y - drFy) * k;
        int nx = lroundf(drFx), ny = lroundf(drFy);
        if (abs(nx - drLx) + abs(ny - drLy) >= 2) drPenPoint(nx, ny);
      } else {
        drPenUp(x, y);
        drDown = false;
      }
    }
    return true;
  }
  // şekiller
  if (ev == TE_DOWN) {
    drAx = x, drAy = y;
    if (drDoc.flags & DRF_GRID) drAx = drGridSnap(x, DR_GRID, 0), drAy = constrain(drGridSnap(y, DR_GRID, DR_CY), DR_CY + 1, SCR_H - 1);
    drHasPrev = false;
    drMoveAt = 0;
    drDown = true;
    return true;
  }
  if (!drDown) return true;
  if (ev == TE_MOVE && millis() - drMoveAt < 35) return true;  // önizleme saniyede ~30 kez
  drMoveAt = millis();
  int bx, by;
  drShapeEnd(x, y, bx, by);
  if (drHasPrev) drPaintBox(drPrev);
  drHasPrev = false;
  if (ev == TE_MOVE) {
    drShapeBox(bx, by, drPrev);
    drHasPrev = true;
    drShapeDraw(bx, by);
    return true;
  }
  drDown = false;
  if (abs(bx - drAx) < 3 && abs(by - drAy) < 3) return true;  // dokunup bırakma: şekil yok
  if (!drRoom(drMirror ? 4 : 2)) return true;
  uint8_t kind = drKindOf(drTool);
  int16_t p[4] = {(int16_t)drAx, (int16_t)drAy, (int16_t)bx, (int16_t)by};
  std::vector<uint16_t> ids{drAdd(kind, drSize, DR_COLS[drColor], p, 2)};
  if (drMirror) {
    int16_t q[4] = {(int16_t)drMirrorX(drAx), (int16_t)drAy, (int16_t)drMirrorX(bx), (int16_t)by};
    ids.push_back(drAdd(kind, drSize, DR_COLS[drColor], q, 2));
  }
  drShapeDraw(bx, by);
  drPushOp(true, ids);
  return true;
}

static int drNearestColor(uint16_t c) {
  int best = 0;
  long bd = LONG_MAX;
  for (int i = 0; i < DR_NC; i++) {
    uint16_t d = DR_COLS[i];
    long dr = ((c >> 11) & 31) - ((d >> 11) & 31), dg = ((c >> 5) & 63) / 2 - ((d >> 5) & 63) / 2, db = (c & 31) - (d & 31);
    long e = dr * dr + dg * dg + db * db;
    if (e < bd) bd = e, best = i;
  }
  return best;
}

static void drAddText(int x, int y, const String& v) {
  int len = v.length();
  if (!len) return;
  if (len > DR_TEXT_MAX) {
    len = DR_TEXT_MAX;
    while (len > 0 && ((uint8_t)v[len] & 0xC0) == 0x80) len--;  // harfin ortasından kesme
  }
  uint8_t font = drFontFor(drSize);
  int n = drTextPts(len);
  if (!drRoom(n)) return;
  y = constrain(y - fontHeight(drFont(font)) / 2, DR_CY + 1, SCR_H - fontHeight(drFont(font)));
  std::vector<int16_t> p(n * 2);
  drTextPack(p.data(), constrain(x, 0, SCR_W - 8), y, font, v.c_str(), len);
  uint16_t k = drAdd(DK_TEXT, font, DR_COLS[drColor], p.data(), n);
  int b[4];
  drBox(drDoc.it[k], drP(drDoc, drDoc.it[k]), b);
  drPaintBox(b);
  drPushOp(true, {k});
}

static bool drExport(const DrDoc& d, const String& stem, String& out);

static void drMenu() {
  bool grid = drDoc.flags & DRF_GRID;
  int pi = 0;
  for (int i = 0; i < 3; i++)
    if (drDoc.paper == DR_PAPERS[i]) pi = i;
  menu("Çizim",
       {String("Kılavuz: ") + (grid ? "açık" : "kapalı"), String("Ayna: ") + (drMirror ? "açık" : "kapalı"),
        String("Sabitleyici: ") + (drStab ? "açık" : "kapalı"), String("Kağıt: ") + DR_PAPER_NAME[pi], "Resim olarak kaydet",
        "Sayfayı temizle"},
       [pi](int c) {
         if (c == 0) drDoc.flags ^= DRF_GRID, drDirty = true;
         else if (c == 1) drMirror = !drMirror;
         else if (c == 2) drStab = !drStab;
         else if (c == 3) drDoc.paper = DR_PAPERS[(pi + 1) % 3], drDirty = true;
         else if (c == 4) {
           String stem = baseName(drPath), out;
           stem = stem.substring(0, stem.lastIndexOf('.'));
           toast(drExport(drDoc, stem, out) ? "Kaydedildi: Fotoğraflar → " + baseName(out) : String("Resim kaydedilemedi"));
           return;
         } else if (c == 5) {
           std::vector<uint16_t> ids;
           for (size_t i = 0; i < drDoc.it.size(); i++)
             if (!drDoc.it[i].hid) drDoc.it[i].hid = true, ids.push_back(i);
           if (!ids.empty()) drPushOp(false, ids);
         }
         if (c >= 0 && c <= 5) {
           drToolbar();
           drPaintAll();
         }
       });
}

// --- Resim olarak kaydetme (BMP, 24 bit): 14 satırlık şeritler ara bellekte çizilir ---
static bool drExport(const DrDoc& d, const String& stem, String& out) {
  if (!sdOk || !ensureDir(DR_EXPORT_DIR)) return false;
  out = joinPath(DR_EXPORT_DIR, stem + ".bmp");
  for (int i = 2; SD.exists(out) && i < 100; i++) out = joinPath(DR_EXPORT_DIR, stem + " (" + String(i) + ").bmp");
  const int W = SCR_W, H = SCR_H - DR_CY, B = 14;
  if (!drMemOk(W * B * 2 + W * 3)) return false;
  TFT_eSprite band(&tft);
  band.setColorDepth(16);
  if (!band.createSprite(W, B)) return false;
  std::unique_ptr<uint8_t[]> row(new (std::nothrow) uint8_t[W * 3]);
  File f = SD.open(out, FILE_WRITE);
  bool ok = row && f;
  if (ok) {
    uint32_t img = W * 3 * H, fsz = 54 + img;
    uint8_t h[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) {
      for (int i = 0; i < 4; i++) h[o + i] = v >> (8 * i);
    };
    put32(2, fsz), put32(10, 54), put32(14, 40), put32(18, W), put32(22, H), put32(34, img), put32(38, 2835), put32(42, 2835);
    h[26] = 1, h[28] = 24;
    ok = f.write(h, 54) == 54;
  }
  for (int b = (H + B - 1) / B - 1; b >= 0 && ok; b--) {  // BMP aşağıdan yukarı
    int top = DR_CY + b * B, rows = std::min(B, H - b * B);
    band.fillSprite(d.paper);
    for (auto& it : d.it) {
      if (it.hid) continue;
      int bb[4];
      drBox(it, drP(d, it), bb);
      if (bb[3] <= top || bb[1] >= top + rows) continue;
      drItemDraw(band, &band, it, drP(d, it), 1, 0, -top, d.paper);
    }
    for (int r = rows - 1; r >= 0 && ok; r--) {
      for (int x = 0; x < W; x++) {
        uint16_t c = band.readPixel(x, r);
        uint8_t r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
        row[x * 3] = b5 << 3 | b5 >> 2, row[x * 3 + 1] = g6 << 2 | g6 >> 4, row[x * 3 + 2] = r5 << 3 | r5 >> 2;
      }
      ok = f.write(row.get(), W * 3) == (size_t)W * 3;
    }
  }
  band.deleteSprite();
  if (f) f.close();
  if (!ok) SD.remove(out);
  else sysLog("çizim", "resim kaydedildi: " + baseName(out));
  return ok;
}

// Fotoğraflar'dan: çizimi tam ekran gösterir
static bool drShowFile(const String& path) {
  DrDoc d;
  if (!drLoad(path, d)) return false;
  tft.fillRect(0, DR_CY, SCR_W, SCR_H - DR_CY, d.paper);
  for (auto& it : d.it) drItemDraw(tft, nullptr, it, drP(d, it), 1, 0, 0, d.paper);
  return true;
}

// --- Liste (küçük resimli, sayfa başına 6) ---
static const int DRG_TW = 96, DRG_TH = 63;
static void drCell(int k, int& x, int& y) { x = 9 + (k % 3) * 104, y = DR_CY + 8 + (k / 3) * 94; }
static int drPages() { return std::max<int>(1, (drList.size() + 5) / 6); }
static String drStem(const String& n) { return n.substring(0, n.lastIndexOf('.')); }

static void drThumb(const String& path, int x, int y) {
  DrDoc d;
  bool ok = drLoad(path, d);
  tft.drawRect(x - 1, y - 1, DRG_TW + 2, DRG_TH + 2, C_LINE);
  tft.fillRect(x, y, DRG_TW, DRG_TH, ok ? d.paper : C_SURF);
  if (!ok) {
    drawIcon(I_PENCIL, x + 38, y + 22, 20, C_TEXT3, C_SURF);
    return;
  }
  const float s = DRG_TW / (float)SCR_W;
  tft.setViewport(x, y, DRG_TW, DRG_TH, false);
  for (auto& it : d.it) drItemDraw(tft, nullptr, it, drP(d, it), s, x, y - DR_CY * s, d.paper);
  tft.resetViewport();
}

static void drLoadList() {
  drList.clear();
  if (!sdOk || !ensureDir(DRAW_DIR)) return;
  std::vector<std::pair<uint32_t, String>> v;
  File d = SD.open(DRAW_DIR);
  for (File f = d.openNextFile(); f && v.size() < 300; f = d.openNextFile()) {
    String n = baseName(f.name());
    if (!f.isDirectory() && extOf(n) == "cizim") v.push_back({(uint32_t)f.getLastWrite(), n});
    f.close();
  }
  d.close();
  std::sort(v.begin(), v.end(), [](const std::pair<uint32_t, String>& a, const std::pair<uint32_t, String>& b) {
    return a.first != b.first ? a.first > b.first : a.second > b.second;
  });
  for (auto& e : v) drList.push_back(e.second);
}

static void drGallery(bool full) {
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
  drPage = constrain(drPage, 0, drPages() - 1);
  fillBg(0, DR_CY, SCR_W, SCR_H - DR_CY);
  for (int k = 0; k < 6; k++) {
    int i = drPage * 6 + k, x, y;
    if (i >= (int)drList.size()) break;
    drCell(k, x, y);
    drThumb(joinPath(DRAW_DIR, drList[i]), x, y);
    text(fit(drStem(drList[i]), F_XS, DRG_TW), x, y + DRG_TH + 5, F_XS, C_TEXT2, C_BG);
  }
  if (drPages() > 1) {
    drawIcon(I_BACK, 104, 220, 16, drPage > 0 ? C_TEXT2 : C_LINE, C_BG);
    text(String(drPage + 1) + " / " + String(drPages()), 160, 228, F_XS, C_TEXT3, C_BG, MC_DATUM);
    drawIcon(I_RIGHT, 200, 220, 16, drPage < drPages() - 1 ? C_TEXT2 : C_LINE, C_BG);
  }
}

static void drEdit(const String& path) {
  drPath = path;
  drOps.clear();
  drOpPos = 0;
  if (SD.exists(path) && !drLoad(path, drDoc)) {  // açılamayan dosyanın üstüne boş sayfa yazılmasın
    drDoc = DrDoc();
    toast("Çizim açılamadı (bellek ya da dosya)");
    return;
  }
  if (!SD.exists(path)) drDoc = DrDoc();
  drEditing = true;
  drDirty = drDown = drSizeDrag = false;
  drPanel = 0;
  drToolbar();
  drPaintAll();
}

static void drDraw(bool full) {
  if (drEditing) {
    drToolbar();
    drPaintAll();
    if (drPanel) drDrawPanel();
    return;
  }
  drGallery(full);
}

static void drNew() {
  if (!sdOk || !ensureDir(DRAW_DIR)) return;
  struct tm t;
  String base = localNow(t) ? "Çizim " + String(t.tm_mday) + " " + String(AY_ADI[t.tm_mon]) + " " + fmtHM(t.tm_hour, t.tm_min) : String("Çizim");
  base.replace(":", ".");
  String p = joinPath(DRAW_DIR, base + ".cizim");
  for (int i = 2; SD.exists(p) && i < 100; i++) p = joinPath(DRAW_DIR, base + " (" + String(i) + ").cizim");
  drEdit(p);
}

static void drTap(int x, int y) {
  if (!drEditing) {
    if (y < HDR_H) {
      if (headerHit(x, y) == 0) drNew();
      return;
    }
    if (y >= 214 && drPages() > 1) {
      int np = constrain(drPage + (x < SCR_W / 2 ? -1 : 1), 0, drPages() - 1);
      if (np != drPage) drPage = np, drGallery(false);
      return;
    }
    for (int k = 0; k < 6; k++) {
      int cx, cy, i = drPage * 6 + k;
      drCell(k, cx, cy);
      if (i < (int)drList.size() && inRect(x, y, cx - 4, cy - 4, DRG_TW + 8, DRG_TH + 26)) drEdit(joinPath(DRAW_DIR, drList[i]));
    }
    return;
  }
  if (y < HDR_H) {
    uint8_t want = x >= 36 && x < 66 ? 1 : (x >= 66 && x < 134 ? 2 : 0);
    if (want) {
      if (drPanel) {
        bool same = drPanel == want;
        drClosePanel();
        if (same) return;
      }
      drPanel = want;
      drToolbar();
      return;
    }
    int h = headerHit(x, y, false);
    if (drPanel && h >= 0) drClosePanel();
    if (h == 2) drUndo();
    else if (h == 1) drRedo();
    else if (h == 0) drMenu();
    return;
  }
  if (drPanel == 1) {
    int i = (x - 7) / 34;
    if (y < DR_CY + DRP_TOOLS_H + 2 && i >= 0 && i < DT_N) {
      drTool = i;
      if (i <= DT_TEXT) drDrawTool = i;
      toast(DT_NAME[i]);
    }
    drClosePanel();
    return;
  }
  if (drPanel == 2) {
    int c = (x - 7) / 31, r = (y - DR_CY - 7) / 28;
    if (y < DR_CY + 62 && c >= 0 && c < 10 && r >= 0 && r < 2) {
      drColor = r * 10 + c;
      if (drTool == DT_ERASER || drTool == DT_PICK) drTool = drDrawTool;  // renk seçmek çizmeye döner
    }
    drClosePanel();
    return;
  }
  if (drTool == DT_TEXT) {
    textInput("Yazı", "", [x, y](const String& v) { drAddText(x, y, v); }, false, DR_TEXT_MAX);
  } else if (drTool == DT_PICK) {
    int k = drHit(x, y, 8);
    if (k < 0) return;
    drColor = drNearestColor(drDoc.it[k].col);
    drTool = drDrawTool;
    drToolbar();
    toast("Renk alındı");
  }
}

static bool drHold(int x, int y) {
  if (drEditing || y < HDR_H) return false;
  for (int k = 0; k < 6; k++) {
    int cx, cy, i = drPage * 6 + k;
    drCell(k, cx, cy);
    if (i >= (int)drList.size() || !inRect(x, y, cx - 4, cy - 4, DRG_TW + 8, DRG_TH + 26)) continue;
    String name = drList[i], p = joinPath(DRAW_DIR, name);
    menu(drStem(name), {"Yeniden adlandır", "Kopyasını oluştur", "Resim olarak kaydet", "Sil"}, [name, p](int c) {
      if (c == 0) {
        textInput("Yeni ad", drStem(name), [p](const String& v) {
          String n = safeName(v);
          n.trim();
          if (!n.length()) return;
          String q = joinPath(DRAW_DIR, n + ".cizim");
          if (q == p) return;
          if (SD.exists(q)) toast("Bu adda bir çizim var");
          else if (!SD.rename(p, q)) toast("Ad değiştirilemedi");
          drLoadList();
          drGallery(true);
        }, false, 60);
      } else if (c == 1) {
        DrDoc d;
        String q = joinPath(DRAW_DIR, drStem(name) + " (kopya).cizim");
        for (int j = 2; SD.exists(q) && j < 100; j++) q = joinPath(DRAW_DIR, drStem(name) + " (kopya " + String(j) + ").cizim");
        toast(drLoad(p, d) && drWrite(q, d) ? String("Kopyalandı") : String("Kopyalanamadı"));
        drLoadList();
        drGallery(true);
      } else if (c == 2) {
        DrDoc d;
        String out;
        toast(drLoad(p, d) && drExport(d, drStem(name), out) ? "Kaydedildi: Fotoğraflar → " + baseName(out)
                                                             : String("Resim kaydedilemedi"));
      } else if (c == 3) {
        dialog("Çizim silinsin mi?", name, {"Vazgeç", "Sil"}, [p](int c2) {
          if (c2 != 1) return;
          SD.remove(p);
          drLoadList();
          drGallery(true);
        });
      }
    });
    return true;
  }
  return false;
}

static void drSwipe(int dir) {
  if (drEditing || drPages() < 2) return;
  int np = constrain(drPage + (dir == SW_LEFT ? 1 : (dir == SW_RIGHT ? -1 : 0)), 0, drPages() - 1);
  if (np != drPage) drPage = np, drGallery(false);
}

// Boşta 20 sn kalınca kayıt (elektrik kesilirse en çok son 20 sn gider)
static void drTick() {
  if (drEditing && drDirty && !drDown && millis() - drEditAt > 20000) {
    if (!drSave()) drEditAt = millis();  // olmadıysa 20 sn sonra yine dener
  }
}

static void drCloseEditor() {
  if (!drSave()) toast("Kaydedilemedi");
  bool empty = true;
  for (auto& it : drDoc.it) empty &= it.hid;
  if (empty) SD.remove(drPath);
  drDoc = DrDoc();  // bellek bırakılır
  std::vector<DrOp>().swap(drOps);
  drOpPos = 0;
  drEditing = drDown = false;
  drPanel = 0;
}

static bool drBack() {
  if (drEditing) {
    if (drPanel) {
      drClosePanel();
      return true;
    }
    drCloseEditor();
    drLoadList();
    drDraw(true);
    return true;
  }
  return false;
}

static void drOpen() {
  drEditing = false;
  drPage = 0;
  drLoadList();
}
static void drClose() {
  if (drEditing) drCloseEditor();
  std::vector<String>().swap(drList);
}

static const App APP_DRAW = {"Çizim", "Çizim", I_PENCIL, drOpen, drDraw, drTap, drHold, drSwipe, drRaw, drTick, drBack, drClose};
