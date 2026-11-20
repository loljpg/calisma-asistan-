#pragma once
// DAXEN VANE tasarım sistemi: renkler, yazı tipleri, yazı ve temel şekiller.
// Obsidyen siyah zemin, grafit yüzeyler, gümüş yazı; tek vurgu rengi çelik mavisi.
#include <TFT_eSPI.h>
#include "../fonts.h"
#include "logo.h"

// --- RGB565 dönüşümü ---
// Yuvarlayarak çevirir (kırpmak renkleri koyulaştırır). Griler nötr kalır: yeşil kanal
// kırmızı/mavinin iki katına oturtulur, yoksa koyu grilerde yeşil ya da mor ton çıkar.
static constexpr uint16_t rgbn(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((((r * 31 + 127) / 255) & 31) << 11) | ((((g * 63 + 127) / 255) & 63) << 5) | (((b * 31 + 127) / 255) & 31));
}
static constexpr int cR5(int v) { return (v * 31 + 127) / 255; }
static constexpr int cAbs(int v) { return v < 0 ? -v : v; }
static constexpr int cG6n(int v) {
  return cAbs(v * 63 - 255 * 2 * cR5(v)) <= cAbs(v * 63 - 255 * (2 * cR5(v) + 1)) ? 2 * cR5(v) : 2 * cR5(v) + 1;
}
static constexpr uint16_t grayn(uint8_t v) { return (uint16_t)((cR5(v) << 11) | (cG6n(v) << 5) | cR5(v)); }

// --- Renk paleti (DAXEN VANE · Obsidian Silver) ---
static const uint16_t C_BG = grayn(0x05);                // #050505 ana arka plan
static const uint16_t C_SURF = grayn(0x15);              // #151515 kart ve panel
static const uint16_t C_SURF2 = grayn(0x10);             // #101010 ikincil yüzey
static const uint16_t C_TEXT = rgbn(0xC5, 0xC9, 0xCE);   // #C5C9CE ana metin (gümüş)
static const uint16_t C_TEXT2 = rgbn(0x85, 0x8B, 0x91);  // #858B91 ikincil metin
static const uint16_t C_TEXT3 = rgbn(0x55, 0x5B, 0x60);  // #555B60 pasif metin
static const uint16_t C_LINE = rgbn(0x30, 0x35, 0x3A);   // #30353A kenarlık
static const uint16_t C_ACC0 = rgbn(0x78, 0x92, 0xA5);   // #7892A5 vurgu (çelik mavisi)
static const uint16_t C_PRESS = grayn(0x22);             // basılı / seçili satır

// Vurgu rengi çalışma anında değişebilir (Ayarlar → Ekran → kapak rengi).
static uint16_t C_ACC = C_ACC0;
static uint16_t C_ACCBG = 0;  // vurgulu yüzey (vurgu, grafitin üstünde %20)

static uint16_t mix565(uint16_t a, uint16_t b, float t) {  // t=0 a, t=1 b
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((int)(ar + (br - ar) * t + .5f) << 11) | ((int)(ag + (bg - ag) * t + .5f) << 5) | (int)(ab + (bb - ab) * t + .5f));
}

// Renkli görüntüyü gümüş tonlara çevirir (telefon uygulama simgeleri için).
static uint16_t silver565(uint16_t c) {
  int r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
  int l = (r * 54 + g * 183 + b * 19) >> 8;
  l = 40 + l * 170 / 255;  // siyaha gömülmesin, parlamasın
  return rgbn(l, l + 3 > 255 ? 255 : l + 3, l + 7 > 255 ? 255 : l + 7);
}

static void themeApply() {
  C_ACC = C_ACC0;
  if (cfg.coverTheme && accentOn) {
    uint8_t a[3] = {accRgb[0], accRgb[1], accRgb[2]};
    float lum = (0.2126f * a[0] + 0.7152f * a[1] + 0.0722f * a[2]) / 255.f;
    float lift = lum < 0.45f ? (0.45f - lum) / (1.f - lum) : 0.f;  // koyu zeminde okunsun
    for (int i = 0; i < 3; i++) a[i] = (uint8_t)(a[i] + (255 - a[i]) * lift);
    C_ACC = rgbn(a[0], a[1], a[2]);
  }
  C_ACCBG = mix565(C_SURF, C_ACC, 0.20f);
}

// Ders renkleri (telefondaki uygulamayla aynı tonlar, gümüşe doğru kısılmış)
static uint16_t dersCol(const String& d) {
  uint16_t c;
  if (d == "Türkçe") c = rgbn(217, 80, 142);
  else if (d == "Edebiyat") c = rgbn(180, 85, 201);
  else if (d == "Matematik") c = rgbn(47, 134, 209);
  else if (d == "Geometri") c = rgbn(75, 104, 214);
  else if (d == "Fizik") c = rgbn(115, 87, 214);
  else if (d == "Kimya") c = rgbn(30, 156, 116);
  else if (d == "Biyoloji") c = rgbn(90, 158, 54);
  else if (d == "Tarih") c = rgbn(217, 115, 43);
  else if (d == "Coğrafya") c = rgbn(201, 148, 21);
  else if (d == "Felsefe") c = rgbn(142, 98, 201);
  else if (d == "Din Kültürü") c = rgbn(168, 133, 54);
  else if (d == "İngilizce") c = rgbn(28, 147, 168);
  else return C_TEXT3;
  return mix565(c, C_TEXT2, 0.45f);
}

// --- Ekran ---
static TFT_eSPI tft;
static TFT_eSprite spr(&tft);  // titremesiz çizim için ara bellek (büyük rakamlar, kedi)
static bool sprOk = false;
static const int SPR_W = 264, SPR_H = 52;

// --- Yazı ---
// F_XS 12 px (durum, etiket), F_SM 15 px (ikincil), F_MD 18 px (ana metin, başlık),
// F_LG 26 px (yalnızca rakam ve işaret), F_XL 64 px ince (saat ve kronometre rakamları)
static const uint8_t* curFont = nullptr;
static void useFont(const uint8_t* f) {
  if (curFont == f) return;
  if (curFont) tft.unloadFont();
  tft.loadFont(f);
  curFont = f;
}

static void text(const String& s, int x, int y, const uint8_t* f, uint16_t fg, uint16_t bg, uint8_t datum = TL_DATUM) {
  useFont(f);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(datum);
  tft.drawString(s, x, y);
}

static int textW(const String& s, const uint8_t* f) {
  useFont(f);
  return tft.textWidth(s);
}

static int fontH(const uint8_t* f) {
  useFont(f);
  return tft.fontHeight();
}

// Sığmazsa sonuna "…" koyarak kısaltır.
static String fit(const String& s, const uint8_t* f, int maxW) {
  if (maxW <= 0) return "";
  if (textW(s, f) <= maxW) return s;
  String t = s;
  while (t.length() && textW(t + "…", f) > maxW) t = utf8Pop(t);
  t.trim();
  return t + "…";
}

// Sözcük kaydırma: en fazla maxLines satır; taşan son satır "…" ile biter.
static std::vector<String> wrapText(const String& s, const uint8_t* f, int maxW, int maxLines = 99) {
  std::vector<String> out;
  String line, word;
  auto flushWord = [&]() {
    if (!word.length()) return;
    String cand = line.length() ? line + " " + word : word;
    if (textW(cand, f) <= maxW) {
      line = cand;
    } else {
      if (line.length()) out.push_back(line);
      line = word;
      while (textW(line, f) > maxW) {  // tek başına sığmayan uzun sözcük bölünür
        String head = line;
        while (head.length() && textW(head, f) > maxW) head = utf8Pop(head);
        if (!head.length()) break;
        out.push_back(head);
        line = line.substring(head.length());
      }
    }
    word = "";
  };
  for (size_t i = 0; i <= s.length(); i++) {
    char c = i < s.length() ? s[i] : '\n';
    if (c == ' ' || c == '\n' || c == '\t') {
      flushWord();
      if (c == '\n') {
        out.push_back(line);
        line = "";
      }
    } else if (c != '\r') {
      word += c;
    }
  }
  if (!out.empty() && !out.back().length() && s.length() && s[s.length() - 1] != '\n') out.pop_back();
  if ((int)out.size() > maxLines) {
    out.resize(maxLines);
    out.back() = fit(out.back() + " …………", f, maxW);
  }
  return out;
}

// --- Temel şekiller ---
static const int R_UI = 3;  // köşe yarıçapı: kontrollü, hafif yuvarlak

static void fillBg(int x, int y, int w, int h) { tft.fillRect(x, y, w, h, C_BG); }

// İnce kenarlıklı grafit yüzey
static void panel(int x, int y, int w, int h, uint16_t fill = C_SURF, uint16_t border = C_LINE) {
  tft.fillRoundRect(x, y, w, h, R_UI, fill);
  if (border != fill) tft.drawRoundRect(x, y, w, h, R_UI, border);
}

static void hline(int x, int y, int w, uint16_t c = C_LINE) { tft.drawFastHLine(x, y, w, c); }

// İnce ilerleme çubuğu: iz çizgi renginde, dolan kısım col
static void progress(int x, int y, int w, int h, int pct, uint16_t col, uint16_t track = C_LINE) {
  pct = constrain(pct, 0, 100);
  int fw = w * pct / 100;
  tft.fillRect(x + fw, y, w - fw, h, track);
  if (fw) tft.fillRect(x, y, fw, h, col);
}

// Büyük ince rakamlar ara bellekte çizilir (saat değişirken titremesin).
static int bigW(const String& s) {
  if (!sprOk) return textW(s, F_XL);
  return spr.textWidth(s);
}
static void bigText(const String& s, int x, int y, int w, int h, uint16_t fg, uint16_t bg, uint8_t align) {
  if (!sprOk) {
    tft.fillRect(x, y, w, h, bg);
    text(s, align == TC_DATUM ? x + w / 2 : (align == TR_DATUM ? x + w : x), y, F_XL, fg, bg, align);
    return;
  }
  w = std::min(w, SPR_W);
  h = std::min(h, SPR_H);
  spr.fillRect(0, 0, w, h, bg);
  spr.setTextColor(fg, bg);
  spr.setTextDatum(align);
  spr.drawString(s, align == TC_DATUM ? w / 2 : (align == TR_DATUM ? w : 0), 0);
  spr.pushSprite(x, y, 0, 0, w, h);
}

// --- Logo (açılış ekranı, Sistem sayfası) ---
// 4 bitlik parlaklık; siyah ile kaynağın gümüşü arasında karıştırılır, renk tonu korunur.
static void drawLogo(const uint8_t* bmp, int w, int h, int x, int y, uint16_t bg = C_BG, float level = 1.f) {
  uint16_t lut[16];
  for (int i = 0; i < 16; i++) lut[i] = i ? grayn((uint8_t)(i * LOGO_PEAK / 15 * level)) : bg;
  int stride = (w + 1) / 2;
  uint16_t row[LOGO_L_W + 1];
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t b = pgm_read_byte(bmp + j * stride + i / 2);
      row[i] = lut[i & 1 ? (b & 15) : (b >> 4)];
    }
    tft.pushImage(x, y + j, w, 1, row);
  }
}
