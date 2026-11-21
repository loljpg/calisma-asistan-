#pragma once
// Yazı tipleri ve logo SD karttaki varlık paketinden okunur (/daxen/sys/varlik.bin; biçim:
// tools/make_assets.py). Yazılıma gömülü değillerdir: paket her sürümün GitHub sayfasındadır,
// eksik ya da eskiyse kart kendisi indirir (sys/assets.h).
//  - Her yazı tipinin karakter dizini (kod, yer, en, ilerleme, sol kayma; karakter başına 7 bayt)
//    ilk kullanımda RAM'e alınır: genişlik ölçmek SD'ye gitmez. Harf görüntüleri (4 bit) çizilirken
//    SD'den okunur.
//  - Ölçüler ve hizalama TFT_eSPI'nin yumuşak yazı tipleriyle aynıdır (.vlw'den çevrildi): yerleşim
//    değişmez.
//  - Paket yoksa ya da bozuksa ekran kitaplığının yerleşik yazısı kullanılır (yalnız ASCII: ç → c,
//    ğ → g …); Wi-Fi ayarlanabilsin, paket inebilsin diye arayüz yine çalışır.
#include "../core/varlik.h"

#define ASSET_PATH "/daxen/sys/varlik.bin"

static uint16_t rd16(const uint8_t* p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// --- Varlık paketi (tek dosya, açık kalır; okumalar kendi kilidinde) ---
static File packF;
static volatile bool packOk = false;
#ifdef HOST
struct PackLock {};
#else
static SemaphoreHandle_t packMtx = nullptr;
struct PackLock {
  PackLock() {
    if (!packMtx) packMtx = xSemaphoreCreateRecursiveMutex();
    xSemaphoreTakeRecursive(packMtx, portMAX_DELAY);
  }
  ~PackLock() { xSemaphoreGiveRecursive(packMtx); }
};
#endif

static bool packRead(uint32_t off, void* buf, size_t n) {
  if (!packOk) return false;
  PackLock l;
  return packF.seek(off) && packF.read((uint8_t*)buf, n) == n;
}

static void fontsReset();

// Paketi açar ve doğrular (boy ve sürüm yazılımın beklediğiyle aynı olmalı)
static bool packOpen() {
  PackLock l;
  if (packF) packF.close();
  packOk = false;
  fontsReset();
  if (!sdOk) return false;
  packF = SD.open(ASSET_PATH);
  if (!packF) return false;
  uint8_t h[8];
  if (packF.size() != ASSET_LEN || packF.read(h, 8) != 8 || memcmp(h, "DXV1", 4) || rd32(h + 4) != ASSET_VER) {
    packF.close();
    return false;
  }
  packOk = true;
  return true;
}

static bool packFind(const char* name, uint32_t& off, uint32_t& len) {
  uint8_t c[4];
  if (!packRead(8, c, 4)) return false;
  uint32_t n = std::min<uint32_t>(rd32(c), 64);
  for (uint32_t i = 0; i < n; i++) {
    uint8_t e[32];
    if (!packRead(12 + i * 32, e, 32)) return false;
    if (!strncmp((const char*)e, name, 24)) {
      off = rd32(e + 24);
      len = rd32(e + 28);
      return true;
    }
  }
  return false;
}

// Adı prefix ile başlayan girdiler (önek atılmış adlarla; ör. mağazadaki uygulamalar)
static std::vector<String> packList(const char* prefix) {
  std::vector<String> v;
  uint8_t c[4];
  if (!packRead(8, c, 4)) return v;
  uint32_t n = std::min<uint32_t>(rd32(c), 64);
  size_t pl = strlen(prefix);
  for (uint32_t i = 0; i < n; i++) {
    char e[25] = {0};
    if (!packRead(12 + i * 32, e, 24)) break;
    if (!strncmp(e, prefix, pl)) v.push_back(String(e + pl));
  }
  return v;
}

// Girdinin ilk len baytı (en çok cap); okunamazsa 0
static size_t packHead(const char* name, char* buf, size_t cap) {
  uint32_t off, len;
  if (!packFind(name, off, len)) return 0;
  size_t k = std::min<size_t>(len, cap);
  return packRead(off, buf, k) ? k : 0;
}

// Girdinin CRC32'si (kurulu dosyayla karşılaştırmak için)
static bool packCrc(const char* name, uint32_t& crc, uint32_t& size) {
  uint32_t off, len;
  if (!packFind(name, off, len)) return false;
  uint8_t b[256];
  crc = 0;
  for (uint32_t p = 0; p < len; p += sizeof b) {
    size_t k = std::min<uint32_t>(sizeof b, len - p);
    if (!packRead(off + p, b, k)) return false;
    crc = crc32Upd(crc, b, k);
  }
  size = len;
  return true;
}

// Paketteki bir dosyayı SD'ye yazar (Lua örnekleri, rehber, gizli özellikler)
static bool packCopy(const char* name, const String& dest) {
  uint32_t off, len;
  if (!packFind(name, off, len)) return false;
  File f = SD.open(dest, FILE_WRITE);
  if (!f) return false;
  uint8_t b[256];
  bool ok = true;
  for (uint32_t p = 0; p < len && ok; p += sizeof b) {
    size_t k = std::min<uint32_t>(sizeof b, len - p);
    ok = packRead(off + p, b, k) && f.write(b, k) == k;
  }
  f.close();
  return ok;
}

// --- Yazı tipleri ---
struct DxFont {
  uint8_t id;
};
typedef const DxFont* Font;
static const DxFont FONT_IDS[5] = {{0}, {1}, {2}, {3}, {4}};
// F_XS 12 px (durum, etiket), F_SM 15 px (ikincil), F_MD 18 px (ana metin, başlık),
// F_LG 26 px (yalnızca rakam ve işaret), F_XL 64 px ince (saat ve kronometre rakamları)
#define F_XS (&FONT_IDS[0])
#define F_SM (&FONT_IDS[1])
#define F_MD (&FONT_IDS[2])
#define F_LG (&FONT_IDS[3])
#define F_XL (&FONT_IDS[4])
static const char* const FONT_PACK[5] = {"f/xs", "f/sm", "f/md", "f/lg", "f/xl"};
static const uint8_t FONT_GLCD[5] = {1, 1, 2, 3, 6};  // yerleşik yazının büyütmesi (paket yokken)

struct FontRt {
  bool tried = false;
  uint16_t n = 0, yAdv = 0, asc = 0, ascHdr = 0, space = 0;
  uint32_t base = 0;
  uint8_t* blk = nullptr;  // kod u16[n], yer u16[n], en u8[n], ilerleme u8[n], sol kayma i8[n]
};
static FontRt fontRt[5];

static void fontsReset() {
  for (auto& r : fontRt) {
    free(r.blk);
    r = FontRt();
  }
}

static FontRt* fontGet(Font f) {
  FontRt& r = fontRt[f->id];
  if (r.blk) return &r;
  if (r.tried || !packOk) return nullptr;
  r.tried = true;
  uint32_t off, len;
  uint8_t h[12];
  if (!packFind(FONT_PACK[f->id], off, len) || !packRead(off, h, 12)) return nullptr;
  uint16_t n = rd16(h);
  uint8_t* b = (uint8_t*)malloc((size_t)n * 7);
  if (!b || !packRead(off + 12, b, (size_t)n * 7)) {
    free(b);
    return nullptr;
  }
  r.n = n;
  r.yAdv = rd16(h + 2);
  r.asc = rd16(h + 4);
  r.ascHdr = rd16(h + 6);
  r.space = rd16(h + 8);
  r.base = off;
  r.blk = b;
  return &r;
}

static int fontFind(const FontRt* r, uint16_t c) {
  int lo = 0, hi = r->n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    uint16_t v = rd16(r->blk + 2 * m);
    if (v == c) return m;
    if (v < c) lo = m + 1;
    else hi = m - 1;
  }
  return -1;
}

// UTF-8 (TFT_eSPI gibi en çok 3 bayt); p ilerler
static uint16_t fontUtf8(const char*& p) {
  uint8_t c = *p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && p[0]) return (c & 0x1F) << 6 | (*p++ & 0x3F);
  if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
    uint16_t v = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
    p += 2;
    return v;
  }
  return c;
}

// Paket yokken yerleşik yazı için: Türkçe harfler sadeleşir, diğer ASCII dışı işaretler '?'
static String fontAscii(const String& s) {
  static const char* const TR[][2] = {{"ç", "c"}, {"Ç", "C"}, {"ğ", "g"}, {"Ğ", "G"}, {"ı", "i"}, {"İ", "I"}, {"ö", "o"},
                                      {"Ö", "O"}, {"ş", "s"}, {"Ş", "S"}, {"ü", "u"}, {"Ü", "U"}, {"·", "-"}, {"…", "..."}};
  String o;
  const char* p = s.c_str();
  while (*p) {
    if ((uint8_t)*p < 0x80) {
      o += *p++;
      continue;
    }
    const char* q = nullptr;
    for (auto& t : TR)
      if (!strncmp(p, t[0], strlen(t[0]))) {
        q = t[1];
        p += strlen(t[0]);
        break;
      }
    if (!q) {
      fontUtf8(p);
      q = "?";
    }
    o += q;
  }
  return o;
}

// Dizinler ağ görevinde yeni paket açılınca bırakılabilir: ölçüm ve çizim paket kilidini tutar
static int fontWidth(Font f, const char* s) {
  PackLock l;
  FontRt* r = fontGet(f);
  if (!r) {
    tft.setTextFont(1);
    tft.setTextSize(FONT_GLCD[f->id]);
    return tft.textWidth(fontAscii(s));
  }
  int w = 0;
  const uint8_t *wt = r->blk + 4 * r->n, *at = r->blk + 5 * r->n, *xt = r->blk + 6 * r->n;
  while (*s) {
    uint16_t c = fontUtf8(s);
    if (c == 0x20) {
      w += r->space;
      continue;
    }
    int g = fontFind(r, c);
    if (g < 0) {
      w += r->space + 1;
      continue;
    }
    int8_t dx = (int8_t)xt[g];
    if (w == 0 && dx < 0) w -= dx;
    w += *s ? at[g] : dx + wt[g];
  }
  return w;
}

static int fontHeight(Font f) {
  PackLock l;
  FontRt* r = fontGet(f);
  return r ? r->yAdv : 8 * FONT_GLCD[f->id];
}

static uint16_t fontBlend(uint8_t a, uint16_t fg, uint16_t bg) {  // TFT_eSPI alphaBlend ile aynı
  uint32_t rxb = bg & 0xF81F;
  rxb += ((fg & 0xF81F) - rxb) * (a >> 2) >> 6;
  uint32_t xgx = bg & 0x07E0;
  xgx += ((fg & 0x07E0) - xgx) * a >> 8;
  return (rxb & 0xF81F) | (xgx & 0x07E0);
}

static inline void fontPut(TFT_eSPI& g, int x, int y, uint16_t* px, int n) { g.pushImage(x, y, n, 1, px); }
static inline void fontPut(TFT_eSprite& g, int x, int y, uint16_t* px, int n) {
  for (int i = 0; i < n; i++) g.drawPixel(x + i, y, px[i]);
}

// Yazı çizer (ekrana ya da ara belleğe). Saydam pikseller değişmez; yarı saydamlar bg ile karışır.
// PAL: 4 bitlik paletli ara bellek; piksel değeri saydamlık düzeyinin kendisidir (0..15).
template <class G, bool PAL = false>
static void fontDraw(G& g, Font f, const String& str, int x, int y, uint16_t fg, uint16_t bg, uint8_t datum) {
  PackLock l;
  FontRt* r = fontGet(f);
  if (!r) {
    g.setTextFont(1);
    g.setTextSize(FONT_GLCD[f->id]);
    g.setTextColor(fg, bg);
    g.setTextDatum(datum);
    g.drawString(fontAscii(str), x, y);
    return;
  }
  const char* s = str.c_str();
  int cw = fontWidth(f, s), ch = r->yAdv;
  switch (datum) {
    case TC_DATUM: x -= cw / 2; break;
    case TR_DATUM: x -= cw; break;
    case ML_DATUM: y -= ch / 2; break;
    case MC_DATUM: x -= cw / 2, y -= ch / 2; break;
    case MR_DATUM: x -= cw, y -= ch / 2; break;
    case BL_DATUM: y -= ch; break;
    case BC_DATUM: x -= cw / 2, y -= ch; break;
    case BR_DATUM: x -= cw, y -= ch; break;
    case L_BASELINE: y -= r->asc; break;
    case C_BASELINE: x -= cw / 2, y -= r->asc; break;
    case R_BASELINE: x -= cw, y -= r->asc; break;
  }
  const uint8_t *wt = r->blk + 4 * r->n, *at = r->blk + 5 * r->n, *xt = r->blk + 6 * r->n;
  uint8_t buf[256];
  uint16_t px[128];
  int cx = x;
  while (*s) {
    uint16_t c = fontUtf8(s);
    if (c == 0x20) {
      cx += r->space;
      continue;
    }
    if (c == '\n') {
      cx = 0;
      y += r->yAdv;
      continue;
    }
    int gi = fontFind(r, c);
    if (gi < 0) {
      g.drawRect(cx, y + r->asc - r->ascHdr, r->space, r->ascHdr, fg);
      cx += r->space + 1;
      continue;
    }
    int w = wt[gi], dx = (int8_t)xt[gi];
    if (cx == 0) cx -= dx;
    uint32_t at0 = r->base + rd16(r->blk + 2 * r->n + 2 * gi);
    uint8_t hd[2];
    if (w && w <= 128 && packRead(at0, hd, 2)) {
      int h = hd[0], gx = cx + dx, gy = y + r->asc - (int8_t)hd[1], rb = (w + 1) / 2, per = sizeof buf / rb;
      for (int y0 = 0; y0 < h; y0 += per) {
        int k = std::min(per, h - y0);
        if (!packRead(at0 + 2 + y0 * rb, buf, k * rb)) break;
        for (int j = 0; j < k; j++) {
          const uint8_t* row = buf + j * rb;
          int run = 0, rs = 0;
          for (int i = 0; i <= w; i++) {
            uint8_t a = i < w ? (i & 1 ? row[i / 2] & 15 : row[i / 2] >> 4) : 0;
            if (a) {
              if (!run) rs = i;
              px[run++] = PAL ? a : a == 15 ? fg : fontBlend(a * 17, fg, bg);
            } else if (run) {
              fontPut(g, gx + rs, gy + y0 + j, px, run);
              run = 0;
            }
          }
        }
      }
    }
    cx += at[gi];
  }
}
