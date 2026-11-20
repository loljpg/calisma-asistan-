#pragma once
// Ekran klavyesi (Türkçe Q). Üç katman: harfler, rakam ve işaretler, ek işaretler.
// Satır başına 24 yarım tuş genişliği vardır (320 px → bir tuş ~26 px). Basılan tuş vurgulanır,
// silme tuşu basılı tutulunca tekrarlar. Kalem ya da tırnakla rahat yazılır.
#include "widgets.h"

enum KbCode : uint8_t { KC_CHAR, KC_SHIFT, KC_BKSP, KC_SPACE, KC_LAYER, KC_LAYER2, KC_ENTER, KC_LEFT, KC_RIGHT };
struct KbKeyDef {
  const char* label;  // KC_CHAR için yazılacak karakter
  uint8_t code, hu;   // hu: yarım tuş genişliği
};
#define KCH(s) {s, KC_CHAR, 2}
static const KbKeyDef KB_L0[4][13] = {
    {KCH("q"), KCH("w"), KCH("e"), KCH("r"), KCH("t"), KCH("y"), KCH("u"), KCH("ı"), KCH("o"), KCH("p"), KCH("ğ"), KCH("ü"), {nullptr, 0, 0}},
    {KCH("a"), KCH("s"), KCH("d"), KCH("f"), KCH("g"), KCH("h"), KCH("j"), KCH("k"), KCH("l"), KCH("ş"), KCH("i"), {nullptr, 0, 0}},
    {{"", KC_SHIFT, 3}, KCH("z"), KCH("x"), KCH("c"), KCH("v"), KCH("b"), KCH("n"), KCH("m"), KCH("ö"), KCH("ç"), {"", KC_BKSP, 3}, {nullptr, 0, 0}},
    {{"123", KC_LAYER, 3}, {",", KC_CHAR, 2}, {"", KC_LEFT, 2}, {"", KC_SPACE, 8}, {"", KC_RIGHT, 2}, {".", KC_CHAR, 2}, {"", KC_ENTER, 5}, {nullptr, 0, 0}},
};
static const KbKeyDef KB_L1[4][13] = {
    {KCH("1"), KCH("2"), KCH("3"), KCH("4"), KCH("5"), KCH("6"), KCH("7"), KCH("8"), KCH("9"), KCH("0"), KCH("-"), KCH("/"), {nullptr, 0, 0}},
    {KCH(":"), KCH(";"), KCH("("), KCH(")"), KCH("₺"), KCH("&"), KCH("@"), KCH("\""), KCH("'"), KCH("+"), KCH("="), {nullptr, 0, 0}},
    {{"#+=", KC_LAYER2, 3}, KCH("."), KCH(","), KCH("?"), KCH("!"), KCH("%"), KCH("*"), KCH("_"), KCH("#"), KCH("~"), {"", KC_BKSP, 3}, {nullptr, 0, 0}},
    {{"ABC", KC_LAYER, 3}, {",", KC_CHAR, 2}, {"", KC_LEFT, 2}, {"", KC_SPACE, 8}, {"", KC_RIGHT, 2}, {".", KC_CHAR, 2}, {"", KC_ENTER, 5}, {nullptr, 0, 0}},
};
static const KbKeyDef KB_L2[4][13] = {
    {KCH("["), KCH("]"), KCH("{"), KCH("}"), KCH("<"), KCH(">"), KCH("^"), KCH("|"), KCH("\\"), KCH("`"), KCH("°"), KCH("€"), {nullptr, 0, 0}},
    {KCH("$"), KCH("…"), KCH("·"), KCH("–"), KCH("—"), KCH("«"), KCH("»"), KCH("“"), KCH("”"), KCH("²"), KCH("³"), {nullptr, 0, 0}},
    {{"123", KC_LAYER2, 3}, KCH("×"), KCH("÷"), KCH("±"), KCH("≤"), KCH("≥"), KCH("½"), KCH("π"), KCH("√"), KCH("•"), {"", KC_BKSP, 3}, {nullptr, 0, 0}},
    {{"ABC", KC_LAYER, 3}, {",", KC_CHAR, 2}, {"", KC_LEFT, 2}, {"", KC_SPACE, 8}, {"", KC_RIGHT, 2}, {".", KC_CHAR, 2}, {"", KC_ENTER, 5}, {nullptr, 0, 0}},
};
#undef KCH

struct Keyboard {
  int y0 = 92, rowH = 37;
  uint8_t layer = 0, shift = 1;  // shift: 0 kapalı, 1 tek harf, 2 kilitli
  bool enterNewline = false;     // çok satırlı metinde ↵ yeni satır
  int pr = -1, pk = -1;          // basılı tuş
  uint32_t downAt = 0, repeatAt = 0;
};

static const KbKeyDef* kbRow(const Keyboard& k, int r) { return k.layer == 0 ? KB_L0[r] : (k.layer == 1 ? KB_L1[r] : KB_L2[r]); }

static int kbRowHu(const KbKeyDef* row) {
  int t = 0;
  for (int i = 0; row[i].label; i++) t += row[i].hu;
  return t;
}

// Tuşun ekran dikdörtgeni
static void kbKeyRect(const Keyboard& k, int r, int i, int& x, int& y, int& w, int& h) {
  const KbKeyDef* row = kbRow(k, r);
  int off = (24 - kbRowHu(row)) * 320 / 48;  // kısa satırlar ortalanır
  int pos = 0;
  for (int j = 0; j < i; j++) pos += row[j].hu;
  x = off + pos * 320 / 24;
  w = (pos + row[i].hu) * 320 / 24 - pos * 320 / 24;
  y = k.y0 + r * k.rowH;
  h = k.rowH;
}

static String kbLabel(const Keyboard& k, const KbKeyDef& d) {
  if (d.code != KC_CHAR) return d.label;
  return k.shift && k.layer == 0 ? trUpper(d.label) : String(d.label);
}

static void kbDrawKey(const Keyboard& k, int r, int i, bool pressed) {
  int x, y, w, h;
  kbKeyRect(k, r, i, x, y, w, h);
  const KbKeyDef& d = kbRow(k, r)[i];
  bool special = d.code != KC_CHAR && d.code != KC_SPACE;
  bool on = (d.code == KC_SHIFT && k.shift) || pressed;
  uint16_t fill = on ? C_ACCBG : (special ? C_SURF2 : C_SURF);
  uint16_t border = on ? C_ACC : C_LINE;
  tft.fillRect(x, y, w, h, C_BG);
  panel(x + 1, y + 2, w - 2, h - 4, fill, border);
  int cx = x + w / 2, cy = y + h / 2;
  switch (d.code) {
    case KC_CHAR: text(kbLabel(k, d), cx, cy + 1, F_MD, C_TEXT, fill, MC_DATUM); break;
    case KC_SHIFT:
      drawIcon(I_UP, cx - 9, cy - 10, 18, k.shift ? C_ACC : C_TEXT2, fill);
      if (k.shift == 2) tft.fillRect(cx - 5, cy + 8, 10, 2, C_ACC);
      break;
    case KC_BKSP:
      drawIcon(I_BACK, cx - 12, cy - 9, 18, C_TEXT2, fill);
      tft.drawFastHLine(cx - 4, cy, 12, C_TEXT2);
      break;
    case KC_LEFT: drawIcon(I_BACK, cx - 8, cy - 8, 16, C_TEXT2, fill); break;
    case KC_RIGHT: drawIcon(I_RIGHT, cx - 8, cy - 8, 16, C_TEXT2, fill); break;
    case KC_SPACE: tft.drawFastHLine(cx - 22, cy + 5, 44, C_TEXT3); break;
    case KC_ENTER:
      if (k.enterNewline) {
        drawIcon(I_BACK, cx - 12, cy - 6, 14, C_TEXT, fill);
        tft.drawFastVLine(cx + 6, cy - 8, 9, C_TEXT);
        tft.drawFastHLine(cx - 6, cy, 13, C_TEXT);
      } else {
        drawIcon(I_CHECK, cx - 9, cy - 9, 18, C_ACC, fill);
      }
      break;
    default: text(d.label, cx, cy + 1, F_SM, C_TEXT2, fill, MC_DATUM); break;
  }
}

static void kbDraw(const Keyboard& k) {
  fillBg(0, k.y0, SCR_W, k.rowH * 4);
  for (int r = 0; r < 4; r++) {
    const KbKeyDef* row = kbRow(k, r);
    for (int i = 0; row[i].label; i++) kbDrawKey(k, r, i, false);
  }
}

static bool kbHit(const Keyboard& k, int x, int y, int& r, int& i) {
  if (y < k.y0 || y >= k.y0 + k.rowH * 4) return false;
  r = (y - k.y0) / k.rowH;
  const KbKeyDef* row = kbRow(k, r);
  int best = -1, bestD = 9999;
  for (int j = 0; row[j].label; j++) {
    int kx, ky, kw, kh;
    kbKeyRect(k, r, j, kx, ky, kw, kh);
    if (x >= kx && x < kx + kw) {
      best = j;
      break;
    }
    int d = x < kx ? kx - x : x - (kx + kw);
    if (d < bestD) {
      bestD = d;
      best = j;
    }
  }
  i = best;
  return best >= 0;
}

// Klavye olayı: yazılacak metin ya da özel tuş
struct KbOut {
  uint8_t code = 0xFF;  // 0xFF: yok
  String s;
};

// Dokunmayı işler; tuş bırakılınca KbOut döner. Katman/büyük harf değişince klavyeyi kendisi çizer.
static KbOut kbTouch(Keyboard& k, uint8_t ev, int x, int y) {
  KbOut out;
  int r, i;
  if (ev == TE_DOWN) {
    if (kbHit(k, x, y, r, i)) {
      k.pr = r;
      k.pk = i;
      k.downAt = millis();
      k.repeatAt = 0;
      kbDrawKey(k, r, i, true);
    }
    return out;
  }
  if (ev == TE_MOVE) {
    if (k.pr < 0) return out;
    if (kbHit(k, x, y, r, i) && (r != k.pr || i != k.pk)) {  // parmak kaydı: yeni tuşa geç
      kbDrawKey(k, k.pr, k.pk, false);
      k.pr = r;
      k.pk = i;
      kbDrawKey(k, r, i, true);
    }
    return out;
  }
  // TE_UP
  if (k.pr < 0) return out;
  r = k.pr;
  i = k.pk;
  k.pr = k.pk = -1;
  const KbKeyDef& d = kbRow(k, r)[i];
  bool repeated = d.code == KC_BKSP && k.repeatAt;
  kbDrawKey(k, r, i, false);
  switch (d.code) {
    case KC_CHAR:
      out.code = KC_CHAR;
      out.s = kbLabel(k, d);
      if (k.shift == 1 && k.layer == 0) {
        k.shift = 0;
        kbDraw(k);
      }
      break;
    case KC_SPACE: out.code = KC_CHAR; out.s = " "; break;
    case KC_SHIFT: {
      static uint32_t lastShift = 0;
      if (k.shift == 1 && millis() - lastShift < 450) k.shift = 2;  // çift dokunuş: kilit
      else k.shift = k.shift ? 0 : 1;
      lastShift = millis();
      kbDraw(k);
      break;
    }
    case KC_LAYER: k.layer = k.layer ? 0 : 1; kbDraw(k); break;
    case KC_LAYER2: k.layer = k.layer == 2 ? 1 : 2; kbDraw(k); break;
    case KC_BKSP:
      if (!repeated) out.code = KC_BKSP;
      break;
    default: out.code = d.code; break;
  }
  return out;
}

// Silme tuşu basılı tutulunca tekrar (ana döngüden çağrılır)
static bool kbRepeat(Keyboard& k) {
  if (k.pr < 0) return false;
  const KbKeyDef& d = kbRow(k, k.pr)[k.pk];
  if (d.code != KC_BKSP) return false;
  uint32_t ms = millis();
  if (ms - k.downAt < 500) return false;
  if (k.repeatAt && ms - k.repeatAt < 90) return false;
  k.repeatAt = ms;
  return true;
}

// --- Metin düzenleme yardımcıları (UTF-8, imleç bayt konumu) ---
static void editInsert(String& s, int& cur, const String& ins) {
  cur = constrain(cur, 0, (int)s.length());
  s = s.substring(0, cur) + ins + s.substring(cur);
  cur += ins.length();
}
static void editBackspace(String& s, int& cur) {
  if (cur <= 0) return;
  int p = utf8Prev(s, cur);
  s = s.substring(0, p) + s.substring(cur);
  cur = p;
}

// ============================================================================
// Tek satırlık metin girişi (tam ekran): başlık, alan, klavye
// ============================================================================
struct KbModal {
  bool active = false;
  String title, value, hint;
  int cur = 0;
  bool password = false, reveal = false;
  int maxLen = 120;
  std::function<void(bool, const String&)> done;
  Keyboard kb;
};
static KbModal kbm;

static const int KBM_FY = 35, KBM_FH = 38;

static void kbmDrawField() {
  fillBg(0, HDR_H, SCR_W, kbm.kb.y0 - HDR_H);
  int fx = 10, fw = SCR_W - 20 - (kbm.password ? 40 : 0);
  panel(fx, KBM_FY, fw, KBM_FH, C_SURF2, C_ACC);
  if (kbm.password) iconButton(SCR_W - 46, KBM_FY, KBM_FH, I_EYE, kbm.reveal ? B_ON : B_NORMAL);
  String shown;
  if (kbm.password && !kbm.reveal) {
    for (int i = 0; i < utf8Count(kbm.value); i++) shown += "•";
  } else {
    shown = kbm.value;
  }
  // İmleçten önceki kısım sığmıyorsa baştan kırp
  String before = kbm.password && !kbm.reveal ? String() : kbm.value.substring(0, kbm.cur);
  if (kbm.password && !kbm.reveal) {
    int n = utf8Count(kbm.value.substring(0, kbm.cur));
    for (int i = 0; i < n; i++) before += "•";
  }
  int avail = fw - 20;
  int skip = 0;
  while (textW(before.substring(skip), F_MD) > avail && skip < (int)before.length()) skip = utf8Next(before, skip);
  String vis = shown.substring(skip);
  vis = fit(vis, F_MD, avail + 2);
  if (!kbm.value.length() && kbm.hint.length()) text(fit(kbm.hint, F_MD, avail), fx + 10, KBM_FY + KBM_FH / 2 + 1, F_MD, C_TEXT3, C_SURF2, ML_DATUM);
  else text(vis, fx + 10, KBM_FY + KBM_FH / 2 + 1, F_MD, C_TEXT, C_SURF2, ML_DATUM);
  int cx = fx + 10 + textW(before.substring(skip), F_MD);
  tft.fillRect(std::min(cx, fx + fw - 4), KBM_FY + 9, 2, KBM_FH - 18, C_ACC);
  String cnt = String(utf8Count(kbm.value)) + "/" + String(kbm.maxLen);
  text(cnt, SCR_W - 12, KBM_FY + KBM_FH + 2, F_XS, C_TEXT3, C_BG, TR_DATUM);
}

static void kbmDraw() {
  header(kbm.title, true, false);
  headerAction(0, I_CHECK, true, false);
  kbmDrawField();
  kbDraw(kbm.kb);
}

static void kbOpen(const String& title, const String& initial, std::function<void(bool, const String&)> done, bool password = false,
                   int maxLen = 120, const String& hint = "") {
  kbm.active = true;
  kbm.title = title;
  kbm.value = initial;
  kbm.cur = initial.length();
  kbm.password = password;
  kbm.reveal = false;
  kbm.maxLen = maxLen;
  kbm.hint = hint;
  kbm.done = done;
  kbm.kb = Keyboard();
  kbm.kb.y0 = 92;
  kbm.kb.rowH = 37;
  kbm.kb.shift = initial.length() || password ? 0 : 1;
  kbmDraw();
}

static void kbmFinish(bool ok) {
  kbm.active = false;
  auto cb = kbm.done;
  String v = kbm.value;
  kbm.done = nullptr;
  if (cb) cb(ok, v);
}

static void kbmTouch(uint8_t ev, int x, int y) {
  if (y < kbm.kb.y0 && kbm.kb.pr < 0) {
    if (ev != TE_UP) return;
    int h = headerHit(x, y, false);
    if (h == -1) {
      kbmFinish(false);
      return;
    }
    if (h == 0) {
      kbmFinish(true);
      return;
    }
    if (kbm.password && x >= SCR_W - 46 && y >= KBM_FY && y < KBM_FY + KBM_FH) {
      kbm.reveal = !kbm.reveal;
      kbmDrawField();
      return;
    }
    if (y >= KBM_FY && y < KBM_FY + KBM_FH) {  // imleci dokunulan yere taşı (basitçe: başa ya da sona yakınlık)
      if (!(kbm.password && !kbm.reveal)) {
        int fx = 20, best = 0, bestD = 9999;
        for (int i = 0; i <= (int)kbm.value.length(); i = i < (int)kbm.value.length() ? utf8Next(kbm.value, i) : i + 1) {
          int d = abs(fx + textW(kbm.value.substring(0, i), F_MD) - x);
          if (d < bestD) {
            bestD = d;
            best = i;
          }
        }
        kbm.cur = best;
        kbmDrawField();
      }
    }
    return;
  }
  KbOut o = kbTouch(kbm.kb, ev, x, y);
  switch (o.code) {
    case KC_CHAR:
      if (utf8Count(kbm.value) < kbm.maxLen) {
        editInsert(kbm.value, kbm.cur, o.s);
        kbmDrawField();
      }
      break;
    case KC_BKSP: editBackspace(kbm.value, kbm.cur); kbmDrawField(); break;
    case KC_LEFT: kbm.cur = utf8Prev(kbm.value, kbm.cur); kbmDrawField(); break;
    case KC_RIGHT: kbm.cur = utf8Next(kbm.value, kbm.cur); kbmDrawField(); break;
    case KC_ENTER: kbmFinish(true); break;
    default: break;
  }
}

static void kbmTick() {
  if (kbRepeat(kbm.kb)) {
    editBackspace(kbm.value, kbm.cur);
    kbmDrawField();
  }
}
