#pragma once
// Notlar: klavyeyle metin notu (.txt) ve el yazısı notu (.el, çizgiler olarak saklanır).
// Notlar SD kartta /daxen/notlar klasöründedir; bilgisayarda da açılabilir (.txt).
#include "../os/shell.h"
#include <SD.h>

#define NOTES_DIR "/daxen/notlar"

struct NoteEntry {
  String file, title;
  time_t at = 0;
  bool hand = false;
  uint32_t size = 0;
};
static std::vector<NoteEntry> ntsList;
static int ntsFirst = 0;
static uint8_t ntsMode = 0;  // 0 liste, 1 metin, 2 el yazısı
static String ntsPath;
static const int NTS_ROW = 42, NTS_ROWS = 5;

static bool ensureDir(const char* d) {
  if (!sdOk) return false;
  if (SD.exists(d)) return true;
  String p;
  String s = d;
  int i = 1;
  while (i <= (int)s.length()) {
    int n = s.indexOf('/', i);
    if (n < 0) n = s.length();
    p = s.substring(0, n);
    if (!SD.exists(p)) SD.mkdir(p);
    i = n + 1;
  }
  return SD.exists(d);
}

static String fmtDate(time_t t) {
  if (t < 1700000000) return "";
  struct tm tm;
  localtime_r(&t, &tm);
  struct tm now;
  if (localNow(now) && now.tm_year == tm.tm_year && now.tm_yday == tm.tm_yday) return "Bugün " + fmtHM(tm.tm_hour, tm.tm_min);
  return String(tm.tm_mday) + " " + String(AY_ADI[tm.tm_mon]).substring(0, 3) + " " + fmtHM(tm.tm_hour, tm.tm_min);
}

static void ntsLoad() {
  ntsList.clear();
  if (!ensureDir(NOTES_DIR)) return;
  File d = SD.open(NOTES_DIR);
  for (File f = d.openNextFile(); f && ntsList.size() < 200; f = d.openNextFile()) {
    String n = baseName(f.name());
    String e = extOf(n);
    if (!f.isDirectory() && (e == "txt" || e == "el")) {
      NoteEntry ne;
      ne.file = n;
      ne.hand = e == "el";
      ne.at = f.getLastWrite();
      ne.size = f.size();
      ne.title = n.substring(0, n.lastIndexOf('.'));
      if (!ne.hand) {  // başlık: ilk dolu satır
        String first;
        for (int i = 0; i < 80 && f.available(); i++) {
          char c = f.read();
          if (c == '\n') {
            first.trim();
            if (first.length()) break;
            first = "";
          } else if (c != '\r') {
            first += c;
          }
        }
        first.trim();
        if (first.length()) ne.title = first;
      }
      ntsList.push_back(ne);
    }
    f.close();
  }
  d.close();
  std::sort(ntsList.begin(), ntsList.end(), [](const NoteEntry& a, const NoteEntry& b) { return a.at > b.at; });
}

static String ntsNewName(const char* ext) {
  struct tm t;
  String base = localNow(t) ? "Not " + String(t.tm_mday) + " " + String(AY_ADI[t.tm_mon]) + " " + fmtHM(t.tm_hour, t.tm_min) : String("Not");
  base.replace(":", ".");
  String p = joinPath(NOTES_DIR, base + ext);
  for (int i = 2; SD.exists(p) && i < 100; i++) p = joinPath(NOTES_DIR, base + " (" + String(i) + ")" + ext);
  return p;
}

// ============================================================================
// Metin düzenleyici
// ============================================================================
static String edText;
static int edCur = 0, edTop = 0;
static bool edDirty = false, edKb = true;
static Keyboard edKbd;
static std::vector<uint16_t> edLines;  // satır başları (bayt)
static const int ED_X = 10, ED_W = 300, ED_LH = 18, ED_Y = HDR_H + 3;

// Karakter genişlikleri (F_SM) önbellekte: satır kaydırma her tuşta hızlı olsun
static uint8_t edCw[256];
static bool edCwOk = false;
static int edCharW(const String& s, int i) {
  uint8_t c = s[i];
  if (c < 0x80) {
    if (!edCwOk) {
      useFont(F_SM);
      for (int k = 0; k < 256; k++) edCw[k] = 0;
      edCwOk = true;
    }
    if (!edCw[c]) {
      char b[2] = {(char)c, 0};
      useFont(F_SM);
      edCw[c] = std::max(1, (int)tft.textWidth(b));
    }
    return edCw[c];
  }
  return textW(s.substring(i, i + utf8Len(c)), F_SM);
}
static int edWidth(const String& s, int a, int b) {
  int w = 0;
  for (int i = a; i < b; i += utf8Len((uint8_t)s[i])) w += edCharW(s, i);
  return w;
}

static void edWrap() {
  edLines.clear();
  edLines.push_back(0);
  int len = edText.length(), ls = 0, lw = 0, brk = -1;
  for (int i = 0; i < len;) {
    uint8_t c = edText[i];
    if (c == '\n') {
      edLines.push_back(i + 1);
      ls = i + 1;
      lw = 0;
      brk = -1;
      i++;
      continue;
    }
    int n = utf8Len(c), w = edCharW(edText, i);
    if (lw + w > ED_W && i > ls) {
      if (brk > ls) {
        edLines.push_back(brk);
        ls = brk;
        lw = edWidth(edText, brk, i);
      } else {
        edLines.push_back(i);
        ls = i;
        lw = 0;
      }
      brk = -1;
    }
    lw += w;
    if (c == ' ') brk = i + 1;
    i += n;
  }
}

static int edLineOf(int pos) {
  int l = 0;
  for (int i = 0; i < (int)edLines.size(); i++)
    if (edLines[i] <= pos) l = i;
  return l;
}

static int edVisible() { return ((edKb ? edKbd.y0 : SCR_H) - ED_Y - 2) / ED_LH; }

static void edDrawText() {
  int bottom = edKb ? edKbd.y0 : SCR_H;
  edWrap();
  int vis = edVisible();
  int cl = edLineOf(edCur);
  if (cl < edTop) edTop = cl;
  if (cl >= edTop + vis) edTop = cl - vis + 1;
  edTop = constrain(edTop, 0, std::max(0, (int)edLines.size() - 1));
  fillBg(0, HDR_H, SCR_W, bottom - HDR_H);
  for (int r = 0; r < vis; r++) {
    int li = edTop + r;
    if (li >= (int)edLines.size()) break;
    int a = edLines[li], b = li + 1 < (int)edLines.size() ? edLines[li + 1] : edText.length();
    String ln = edText.substring(a, b);
    while (ln.endsWith("\n")) ln.remove(ln.length() - 1);
    int y = ED_Y + r * ED_LH;
    if (ln.length()) text(ln, ED_X, y, F_SM, C_TEXT, C_BG);
    if (li == cl) {
      int cx = ED_X + edWidth(edText, a, std::min(edCur, (int)(a + ln.length())));
      tft.fillRect(cx, y + 1, 2, ED_LH - 2, C_ACC);
    }
  }
  if (!edText.length()) text("Yazmaya başla…", ED_X + 4, ED_Y, F_SM, C_TEXT3, C_BG);
  scrollBar(HDR_H, bottom - HDR_H, edTop, vis, edLines.size());
  if (!edKb) text(String(utf8Count(edText)) + " karakter", 308, SCR_H - 12, F_XS, C_TEXT3, C_BG, MR_DATUM);
}

static void edDrawAll() {
  header(fit(baseName(ntsPath).substring(0, baseName(ntsPath).lastIndexOf('.')), F_MD, 200), true, false);
  headerAction(0, I_KEYBOARD, edKb, false);
  edDrawText();
  if (edKb) kbDraw(edKbd);
}

static bool edSave() {
  if (!edDirty) return true;
  File f = SD.open(ntsPath, FILE_WRITE);
  if (!f) return false;
  f.print(edText);
  f.close();
  edDirty = false;
  return true;
}

static void edOpen(const String& path) {
  ntsPath = path;
  edText = "";
  edDirty = false;
  File f = SD.open(path);
  if (f) {
    size_t n = f.size();
    if (n > 12000) {
      f.close();
      alertBox("Not çok uzun", "12 KB'tan büyük metinler Okuyucu'da açılır.");
      openFileWith(path);
      return;
    }
    edText.reserve(n + 64);
    edText = f.readString();
    f.close();
    edText.replace("\r", "");
  }
  edCur = edText.length();
  edTop = 0;
  edKb = !edText.length();
  edKbd = Keyboard();
  edKbd.y0 = 104;
  edKbd.rowH = 34;
  edKbd.enterNewline = true;
  edKbd.shift = edText.length() ? 0 : 1;
  ntsMode = 1;
  edDrawAll();
}

// Metne dokunulan yer → imleç
static void edTapText(int x, int y) {
  int r = (y - ED_Y) / ED_LH, li = edTop + r;
  if (li >= (int)edLines.size()) {
    edCur = edText.length();
    return;
  }
  int a = edLines[li], b = li + 1 < (int)edLines.size() ? edLines[li + 1] : edText.length();
  if (b > a && edText[b - 1] == '\n') b--;
  int best = a, bx = ED_X;
  for (int i = a; i <= b; i = i < b ? utf8Next(edText, i) : b + 1) {
    int cx = ED_X + edWidth(edText, a, i);
    if (abs(cx - x) < abs(bx - x)) {
      best = i;
      bx = cx;
    }
  }
  edCur = best;
}

static bool edRaw(uint8_t ev, int x, int y) {
  if (y < HDR_H && ev == TE_DOWN) return false;
  if (edKb && (y >= edKbd.y0 || edKbd.pr >= 0)) {
    KbOut o = kbTouch(edKbd, ev, x, y);
    switch (o.code) {
      case KC_CHAR: editInsert(edText, edCur, o.s); edDirty = true; edDrawText(); break;
      case KC_BKSP: editBackspace(edText, edCur); edDirty = true; edDrawText(); break;
      case KC_ENTER: editInsert(edText, edCur, "\n"); edDirty = true; edDrawText(); break;
      case KC_LEFT: edCur = utf8Prev(edText, edCur); edDrawText(); break;
      case KC_RIGHT: edCur = utf8Next(edText, edCur); edDrawText(); break;
      default: break;
    }
    return true;
  }
  static int downY = 0;
  if (ev == TE_DOWN) downY = y;
  if (ev == TE_UP) {
    if (abs(y - downY) > 30) {  // sürükleme: kaydır
      int step = (downY - y) / ED_LH;
      edTop = constrain(edTop + step, 0, std::max(0, (int)edLines.size() - edVisible()));
      int a = edLines[edTop];
      if (edLineOf(edCur) < edTop || edLineOf(edCur) >= edTop + edVisible()) edCur = a;
      edDrawText();
    } else if (!edKb) {
      edKb = true;
      edTapText(x, y);
      edDrawAll();
    } else {
      edTapText(x, y);
      edDrawText();
    }
  }
  return true;
}

// ============================================================================
// El yazısı
// ============================================================================
struct HwStroke {
  uint8_t w;
  uint16_t start, n;  // hwPts içinde
};
static std::vector<HwStroke> hwStrokes;
static std::vector<int16_t> hwPts;  // x, y çiftleri
static uint8_t hwPen = 2;           // 2 ince, 4 kalın
static bool hwEraser = false, hwDirty = false;
static int hwLx = 0, hwLy = 0;
static const int HW_MAXPTS = 5000;

static void hwRules() {
  for (int y = HDR_H + 34; y < SCR_H; y += 30) tft.drawFastHLine(8, y, SCR_W - 16, grayn(0x16));
}
static void hwDrawStroke(const HwStroke& s, uint16_t col) {
  const int16_t* p = &hwPts[s.start * 2];
  if (s.n == 1) {
    tft.fillSmoothCircle(p[0], p[1], std::max(1, s.w / 2), col, C_BG);
    return;
  }
  for (int i = 1; i < s.n; i++) tft.drawWideLine(p[i * 2 - 2], p[i * 2 - 1], p[i * 2], p[i * 2 + 1], s.w, col, C_BG);
}
static void hwDrawCanvas() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  hwRules();
  for (auto& s : hwStrokes) hwDrawStroke(s, C_TEXT);
}
static void hwDrawHeader() {
  header(fit(baseName(ntsPath).substring(0, baseName(ntsPath).lastIndexOf('.')), F_MD, 110), true, false);
  headerAction(0, I_TRASH, false, false);
  headerAction(1, I_UNDO, false, false);
  headerAction(2, I_ERASER, hwEraser, false);
  int x = hdrActX(3, false);
  tft.fillRect(x, 0, 30, HDR_H - 1, C_BG);
  drawIcon(I_PENCIL, x + 5, 5, 20, !hwEraser ? C_ACC : C_TEXT2, C_BG);
  tft.fillRect(x + 22, 20, hwPen + 1, hwPen + 1, !hwEraser ? C_ACC : C_TEXT2);
}

static bool hwSave() {
  if (!hwDirty) return true;
  File f = SD.open(ntsPath, FILE_WRITE);
  if (!f) return false;
  uint8_t hdr[7] = {'D', 'X', 'E', 'L', 1, (uint8_t)(hwStrokes.size() & 255), (uint8_t)(hwStrokes.size() >> 8)};
  f.write(hdr, 7);
  for (auto& s : hwStrokes) {
    uint8_t h[3] = {s.w, (uint8_t)(s.n & 255), (uint8_t)(s.n >> 8)};
    f.write(h, 3);
    f.write((const uint8_t*)&hwPts[s.start * 2], s.n * 4);
  }
  f.close();
  hwDirty = false;
  return true;
}

// Çizim dosyasını okur (Notlar, Çizim ve Fotoğraflar kullanır)
static bool hwLoadFile(const String& path, std::vector<HwStroke>& st, std::vector<int16_t>& pts, std::vector<uint16_t>* cols = nullptr) {
  st.clear();
  pts.clear();
  File f = SD.open(path);
  if (!f) return false;
  uint8_t hdr[7];
  if (f.read(hdr, 7) != 7 || memcmp(hdr, "DXEL", 4)) {
    f.close();
    return false;
  }
  bool color = hdr[4] >= 2;
  int n = hdr[5] | (hdr[6] << 8);
  for (int i = 0; i < n; i++) {
    uint8_t h[3];
    if (f.read(h, 3) != 3) break;
    uint16_t col = C_TEXT;
    if (color) {
      uint8_t cb[2];
      f.read(cb, 2);
      col = cb[0] | (cb[1] << 8);
    }
    HwStroke s{h[0], (uint16_t)(pts.size() / 2), (uint16_t)(h[1] | (h[2] << 8))};
    if (pts.size() / 2 + s.n > HW_MAXPTS) break;
    size_t at = pts.size();
    pts.resize(at + s.n * 2);
    if (f.read((uint8_t*)&pts[at], s.n * 4) != (size_t)s.n * 4) {
      pts.resize(at);
      break;
    }
    st.push_back(s);
    if (cols) cols->push_back(col);
  }
  f.close();
  return true;
}

static void hwOpen(const String& path) {
  ntsPath = path;
  hwLoadFile(path, hwStrokes, hwPts);
  hwDirty = false;
  hwEraser = false;
  ntsMode = 2;
  hwDrawHeader();
  hwDrawCanvas();
}

// Silgi: dokunulan yerin yakınından geçen çizgiyi bütünüyle siler
static void hwEraseAt(int x, int y) {
  for (int k = hwStrokes.size() - 1; k >= 0; k--) {
    const HwStroke& s = hwStrokes[k];
    const int16_t* p = &hwPts[s.start * 2];
    for (int i = 0; i < s.n; i++)
      if (abs(p[i * 2] - x) < 9 && abs(p[i * 2 + 1] - y) < 9) {
        int start = s.start, n = s.n;
        hwPts.erase(hwPts.begin() + start * 2, hwPts.begin() + (start + n) * 2);
        hwStrokes.erase(hwStrokes.begin() + k);
        for (auto& o : hwStrokes)
          if (o.start > start) o.start -= n;
        hwDirty = true;
        hwDrawCanvas();
        return;
      }
  }
}

static bool hwRaw(uint8_t ev, int x, int y) {
  if (ev == TE_DOWN && y < HDR_H) return false;
  y = std::max(y, HDR_H + 2);
  if (hwEraser) {
    if (ev != TE_UP) hwEraseAt(x, y);
    return true;
  }
  if (ev == TE_DOWN) {
    if ((int)hwPts.size() / 2 >= HW_MAXPTS) {
      toast("Sayfa doldu; yeni bir not aç");
      return true;
    }
    hwStrokes.push_back(HwStroke{hwPen, (uint16_t)(hwPts.size() / 2), 1});
    hwPts.push_back(x);
    hwPts.push_back(y);
    hwLx = x;
    hwLy = y;
    tft.fillSmoothCircle(x, y, std::max(1, hwPen / 2), C_TEXT, C_BG);
    hwDirty = true;
  } else if (ev == TE_MOVE && !hwStrokes.empty()) {
    if (abs(x - hwLx) + abs(y - hwLy) < 3 || (int)hwPts.size() / 2 >= HW_MAXPTS) return true;
    tft.drawWideLine(hwLx, hwLy, x, y, hwPen, C_TEXT, C_BG);
    hwPts.push_back(x);
    hwPts.push_back(y);
    hwStrokes.back().n++;
    hwLx = x;
    hwLy = y;
  }
  return true;
}

// ============================================================================
// Liste
// ============================================================================
static void ntsDrawList() {
  if (!sdOk) {
    emptyState(HDR_H, SCR_H - HDR_H, I_SD, "SD kart gerekli", "Notlar SD kartta saklanır.");
    return;
  }
  if (ntsList.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_NOTES, "Not yok", "Sağ üstteki + ile metin ya da el yazısı notu ekle.");
    return;
  }
  int n = ntsList.size();
  ntsFirst = constrain(ntsFirst, 0, std::max(0, n - NTS_ROWS));
  for (int r = 0; r < NTS_ROWS; r++) {
    int i = ntsFirst + r, y = HDR_H + r * NTS_ROW;
    if (i >= n) {
      fillBg(0, y, SCR_W, NTS_ROW);
      continue;
    }
    const NoteEntry& e = ntsList[i];
    String sub = fmtDate(e.at);
    sub += sub.length() ? " · " : "";
    sub += e.hand ? String("El yazısı") : fmtBytes(e.size);
    listRow(y, NTS_ROW, e.hand ? I_PENCIL : I_TEXT, e.title, sub, "", false);
  }
  scrollBar(HDR_H, NTS_ROW * NTS_ROWS, ntsFirst, NTS_ROWS, n);
}

static void ntsDraw(bool full) {
  if (ntsMode == 1) {
    if (full) edDrawAll();
    else edDrawText();
    return;
  }
  if (ntsMode == 2) {
    if (full) hwDrawHeader();
    hwDrawCanvas();
    return;
  }
  if (full) {
    header("Notlar");
    if (sdOk) headerAction(0, I_PLUS);
  }
  ntsDrawList();
}

static void ntsCloseEditor() {
  bool ok = ntsMode == 1 ? edSave() : hwSave();
  if (!ok) toast("Kaydedilemedi (SD kart?)");
  if (ntsMode == 1 && !edText.length()) SD.remove(ntsPath);  // boş not kalmasın
  if (ntsMode == 2 && hwStrokes.empty()) SD.remove(ntsPath);
  edText = String();
  hwStrokes.clear();
  hwPts.clear();
  hwStrokes.shrink_to_fit();
  hwPts.shrink_to_fit();
  ntsMode = 0;
}

static void ntsTap(int x, int y) {
  if (ntsMode == 1) {
    if (y < HDR_H && headerHit(x, y, false) == 0) {
      edKb = !edKb;
      edDrawAll();
    }
    return;
  }
  if (ntsMode == 2) {
    if (y >= HDR_H) return;
    int h = headerHit(x, y, false);
    if (h == 0) {
      dialog("Sayfa temizlensin mi?", "Bu nottaki bütün çizgiler silinir.", {"Vazgeç", "Temizle"}, [](int c) {
        if (c != 1) return;
        hwStrokes.clear();
        hwPts.clear();
        hwDirty = true;
      });
    } else if (h == 1 && !hwStrokes.empty()) {
      const HwStroke s = hwStrokes.back();
      hwPts.resize(s.start * 2);
      hwStrokes.pop_back();
      hwDirty = true;
      hwDrawCanvas();
    } else if (h == 2) {
      hwEraser = !hwEraser;
      hwDrawHeader();
    } else if (h == 3) {
      if (hwEraser) hwEraser = false;
      else hwPen = hwPen == 2 ? 4 : 2;
      hwDrawHeader();
    }
    return;
  }
  if (y < HDR_H) {
    if (headerHit(x, y) == 0 && sdOk)
      menu("Yeni not", {"Metin notu (klavye)", "El yazısı notu"}, [](int c) {
        if (c < 0 || !ensureDir(NOTES_DIR)) return;
        if (c == 0) {
          String p = ntsNewName(".txt");
          File f = SD.open(p, FILE_WRITE);
          if (f) f.close();
          edOpen(p);
        } else if (c == 1) {
          String p = ntsNewName(".el");
          hwOpen(p);
          hwDirty = false;
        }
      });
    return;
  }
  int r = (y - HDR_H) / NTS_ROW, i = ntsFirst + r;
  if (r >= NTS_ROWS || i >= (int)ntsList.size()) return;
  String p = joinPath(NOTES_DIR, ntsList[i].file);
  if (ntsList[i].hand) hwOpen(p);
  else edOpen(p);
}

static bool ntsHold(int x, int y) {
  if (ntsMode != 0) return false;
  int r = (y - HDR_H) / NTS_ROW, i = ntsFirst + r;
  if (y < HDR_H || r >= NTS_ROWS || i >= (int)ntsList.size()) return false;
  NoteEntry e = ntsList[i];
  menu(e.title, {"Yeniden adlandır", "Sil"}, [e](int c) {
    String p = joinPath(NOTES_DIR, e.file);
    if (c == 0) {
      String stem = e.file.substring(0, e.file.lastIndexOf('.')), ext = e.file.substring(e.file.lastIndexOf('.'));
      textInput("Not adı", stem, [p, ext](const String& v) {
        String nn = safeName(v);
        if (!nn.length()) return;
        String dst = joinPath(NOTES_DIR, nn + ext);
        if (SD.exists(dst)) toast("Bu adda bir not var");
        else SD.rename(p, dst);
        ntsLoad();
      }, false, 50);
    } else if (c == 1) {
      dialog("Not silinsin mi?", e.title, {"Vazgeç", "Sil"}, [p](int k) {
        if (k != 1) return;
        SD.remove(p);
        ntsLoad();
        ntsDraw(true);
      });
    }
  });
  return true;
}

static void ntsSwipe(int dir) {
  if (ntsMode == 1 && (dir == SW_UP || dir == SW_DOWN) && !edKb) {
    edTop = constrain(edTop + (dir == SW_UP ? 8 : -8), 0, std::max(0, (int)edLines.size() - edVisible()));
    int a = edLines[edTop];
    edCur = constrain(edCur, a, (int)edText.length());
    if (edLineOf(edCur) >= edTop + edVisible()) edCur = a;
    edDrawText();
    return;
  }
  if (ntsMode != 0 || (dir != SW_UP && dir != SW_DOWN)) return;
  int n = ntsList.size();
  int nf = constrain(ntsFirst + (dir == SW_UP ? NTS_ROWS - 1 : -(NTS_ROWS - 1)), 0, std::max(0, n - NTS_ROWS));
  if (nf != ntsFirst) {
    ntsFirst = nf;
    ntsDrawList();
  }
}

static bool ntsRaw(uint8_t ev, int x, int y) {
  if (ntsMode == 1) return edRaw(ev, x, y);
  if (ntsMode == 2) return hwRaw(ev, x, y);
  return false;
}

static void ntsTick() {
  if (ntsMode == 1 && edKb && kbRepeat(edKbd)) {
    editBackspace(edText, edCur);
    edDirty = true;
    edDrawText();
  }
}

static bool ntsBack() {
  if (ntsMode != 0) {
    ntsCloseEditor();
    ntsLoad();
    ntsDraw(true);
    return true;
  }
  return false;
}

static void ntsOpen() {
  ntsMode = 0;
  ntsFirst = 0;
  ntsLoad();
}

static void ntsClose() {
  if (ntsMode != 0) ntsCloseEditor();
}

static const App APP_NOTES = {"Notlar", "Notlar", I_NOTES, ntsOpen, ntsDraw, ntsTap, ntsHold, ntsSwipe, ntsRaw, ntsTick, ntsBack, ntsClose};
