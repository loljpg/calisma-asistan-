#pragma once
// Ana ekrandaki piksel kedi.
#include "../ui/widgets.h"

// Kedi kareleri (tools ile üretildi). O: çizgi, W: tüy, G: gölge, K: koyu gölge, E: göz, P: pembe, .: boş
static const int CAT_W = 19, CAT_H = 13;
enum CatFrame { CF_WALK1, CF_WALK2, CF_RUN, CF_BAT, CF_SIT, CF_SLEEP };
static const char* const CAT_FRAMES[][CAT_H] = {
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO.....OWWWWEWWO",
     "..OWWO....OWWWWWWPO",
     "..OWWWOOOOWWWWWWWO.",
     "...OWWWWWWWWWWWWO..",
     "...OGWWWWWWWWWWGO..",
     "...OGGWWWWWWWWGGO..",
     "...OWKOKWOOOWKOWO..",
     "...OWKOKWO.OWKOWO..",
     "....OO.OO...OO.O..."},  // walk1
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO.....OWWWWEWWO",
     "..OWWO....OWWWWWWPO",
     "..OWWWOOOOWWWWWWWO.",
     "...OWWWWWWWWWWWWO..",
     "...OGWWWWWWWWWWGO..",
     "...OGGWWWWWWWWGGO..",
     "....OWKWOOOOOWKWO..",
     "....OWKWO...OWKWO..",
     ".....OOO.....OOO..."},  // walk2
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO....OWWWWWEWWO",
     "..OWWO...OWWWWWWWPO",
     "..OWWWOOOWWWWWWWWO.",
     "...OWWWWWWWWWWWWO..",
     "...OGWWWWWWWWWWGO..",
     "..OGGGWWWWWWWGGWWO.",
     ".OWWOGGGOOOOGGOOWWO",
     "OWWO.OOO....OO..OWO",
     ".OO..............O."},  // run
    {"..OO........O...O..",
     ".OWWO......OWO.OWO.",
     "OWWWWO....OWKOOPWO.",
     "OWWWO.....OWWWWWWO.",
     ".OWWO.....OWWWWEWWO",
     "..OWWO....OWWWWWWPO",
     "..OWWWOOOOWWWWWWWO.",
     "...OWWWWWWWWWWWWWWO",
     "...OGWWWWWWWWWWGOWO",
     "...OGGWWWWWWWWGGOO.",
     "...OWKOKWOOOWKOO...",
     "...OWKOKWO.OWKO....",
     "....OO.OO...OO....."},  // bat
    {".........O...O.....",
     "........OWO.OWO....",
     "........OWKOOPWO...",
     "........OWWWWWWO...",
     "..OO....OWWWWEWWO..",
     ".OWWO...OWWWWWWPO..",
     "OWWWWO.OWWWWWWWO...",
     "OWWWO.OWWWWWWWWO...",
     "OWWO.OWWWWWWWWWO...",
     ".OWWOOGWWWWWWWWO...",
     "..OWWGGWWWWWWGWO...",
     "...OGGGGWWWWGGWO...",
     "....OOOOOOOOOOO...."},  // sit
    {"...................",
     "...................",
     "...................",
     "...................",
     "............O...O..",
     ".......OOOOOWO.OWO.",
     "....OOOWWWWWWKOPWO.",
     "...OWWWWWWWWWWWWWO.",
     "..OWWWWWWWWWWKKWWWO",
     ".OWWGWWWWWWWWWWWWPO",
     ".OWWGGWWWWWWWWWWWO.",
     "..OWWWGGGGGGGGGWO..",
     "...OOOOOOOOOOOOO..."},  // sleep
};

// --- Ana ekrandaki kedi ve topu ---

// Saatin altındaki şeritte kendi kendine dolaşır, oturur, topunu kovalar ve patiyle iter.
// Gece (22:00–07:00) uyur. Üstüne dokununca sevinir. DAXEN renkleri: gümüş tüy, grafit çizgi,
// çelik mavisi top.
static const int CAT_S = 2;                          // her sanat pikseli 2×2 ekran pikseli
static const int ZONE_TOP = 100, ZONE_FLOOR = 127;   // şerit: 100..129, ayakların bastığı çizgi 127
static const int ZONE_H = 30;
enum CatState { CAT_SIT, CAT_WALK, CAT_CHASE, CAT_BAT, CAT_SLEEP, CAT_HAPPY };
struct CatS {
  float x = 120, target = 120, bx = 220, bvx = 0;
  int dir = 1, hop = 0;
  CatState st = CAT_SIT;
  uint32_t until = 0, tick = 0, happyAt = 0;
  int lastL = -1, lastR = -1, lastBL = -1, lastBR = -1, lastFrame = -1, lastHop = 0;
  bool lastZ = false;
};
static CatS cat;

static uint16_t catColor(char c) {
  switch (c) {
    case 'O': return rgbn(92, 98, 104);
    case 'W': return rgbn(206, 210, 215);
    case 'G': return rgbn(158, 163, 169);
    case 'K': return rgbn(118, 123, 129);
    case 'E': return C_ACC;
    case 'P': return rgbn(150, 132, 138);
    default: return 0;
  }
}

static int catFrameNow() {
  switch (cat.st) {
    case CAT_WALK: return (cat.tick / 4) % 2 ? CF_WALK2 : CF_WALK1;
    case CAT_CHASE: return (cat.tick / 3) % 2 ? CF_RUN : CF_WALK2;
    case CAT_BAT: return CF_BAT;
    case CAT_SLEEP: return CF_SLEEP;
    default: return CF_SIT;
  }
}

static int catLeft() { return (int)cat.x - CAT_W * CAT_S / 2; }

// Küçük piksel desenleri (uyku "z"si ve kalp)
static void catPattern(int x, int y, const char* const* rows, int n, uint16_t col) {
  for (int r = 0; r < n; r++)
    for (int c = 0; rows[r][c]; c++)
      if (rows[r][c] == 'X') spr.fillRect(x + c * 2, y + r * 2, 2, 2, sprC(col));
}
static const char* const PAT_Z[] = {"XXX", "..X", ".X.", "XXX"};
static const char* const PAT_HEART[] = {".X.X.", "XXXXX", ".XXX.", "..X.."};

// Şeridin [x0, x0+w) kısmını ara bellekte çizip ekrana basar.
static void catRender(int x0, int w) {
  int x1 = std::min(SCR_W, x0 + w);
  x0 = std::max(0, x0);
  if (!sprOk || x1 <= x0) return;
  while (x0 < x1) {
    int ww = std::min(x1 - x0, SPR_W);
    sprPalBegin();  // ara bellek 4 bit paletli (ui/theme.h): renkler sprC ile
    spr.fillRect(0, 0, ww, ZONE_H, sprC(C_BG));
    int cx = (int)cat.x;
    uint16_t shadow = C_SURF;
    int fy = ZONE_FLOOR - ZONE_TOP;
    if (cat.st != CAT_SLEEP) spr.fillRect(cx - 13 - x0, fy, 26, 2, sprC(shadow));
    else spr.fillRect(cx - 16 - x0, fy, 32, 2, sprC(shadow));
    // top
    int bx = (int)cat.bx, by = fy - 4;
    spr.fillRect(bx - 4 - x0, fy + 1, 9, 1, sprC(shadow));
    spr.fillCircle(bx - x0, by, 4, sprC(C_ACC));
    float a = cat.bx / 4.0f;
    spr.fillRect(bx - x0 + (int)roundf(cosf(a) * 2) - 1, by + (int)roundf(sinf(a) * 2) - 1, 2, 2, sprC(C_TEXT));
    // kedi
    int f = catFrameNow();
    int left = catLeft(), top = ZONE_FLOOR + 1 - CAT_H * CAT_S - cat.hop;
    for (int r = 0; r < CAT_H; r++) {
      const char* row = CAT_FRAMES[f][r];
      for (int c = 0; c < CAT_W; c++) {
        char ch = row[cat.dir > 0 ? c : CAT_W - 1 - c];
        if (ch == '.') continue;
        spr.fillRect(left + c * CAT_S - x0, top + r * CAT_S - ZONE_TOP, CAT_S, CAT_S, sprC(catColor(ch)));
      }
    }
    if (cat.st == CAT_SLEEP) {
      int zx = cat.dir > 0 ? left + 22 : left + 8;
      catPattern(zx - x0, top + 2 - ZONE_TOP, PAT_Z, 4, C_TEXT3);
      if ((cat.tick / 3) % 2) catPattern(zx + 9 * cat.dir - x0, top - ZONE_TOP - 1, PAT_Z, 4, C_LINE);
    } else if (cat.st == CAT_HAPPY) {
      int hx = cat.dir > 0 ? left + CAT_W * CAT_S + 1 : left - 11;
      catPattern(hx - x0, top + 2 - ZONE_TOP, PAT_HEART, 4, C_ACC);
    }
    spr.pushSprite(x0, ZONE_TOP, 0, 0, ww, ZONE_H);
    x0 += ww;
  }
}

static void catDrawAll() {
  cat.lastL = cat.lastR = cat.lastBL = cat.lastBR = -1;
  catRender(0, SCR_W);
}

// Yalnızca değişen yer yeniden çizilir (eski ve yeni konumları kapsayan dar şerit).
static void catFlush() {
  int f = catFrameNow();
  int l = catLeft() - 14, r = catLeft() + CAT_W * CAT_S + 14;
  int bl = (int)cat.bx - 7, br = (int)cat.bx + 7;
  bool z = (cat.tick / 3) % 2;
  bool same = l == cat.lastL && bl == cat.lastBL && f == cat.lastFrame && cat.hop == cat.lastHop &&
              (cat.st != CAT_SLEEP || z == cat.lastZ);
  if (same) return;
  int a0 = cat.lastL < 0 ? l : std::min(l, cat.lastL), a1 = std::max(r, cat.lastR);
  int b0 = cat.lastBL < 0 ? bl : std::min(bl, cat.lastBL), b1 = std::max(br, cat.lastBR);
  if (b0 <= a1 && b1 >= a0) {
    catRender(std::min(a0, b0), std::max(a1, b1) - std::min(a0, b0));
  } else {
    catRender(a0, a1 - a0);
    catRender(b0, b1 - b0);
  }
  cat.lastL = l;
  cat.lastR = r;
  cat.lastBL = bl;
  cat.lastBR = br;
  cat.lastFrame = f;
  cat.lastHop = cat.hop;
  cat.lastZ = z;
}

static void catUpdate(bool night) {
  uint32_t ms = millis();
  cat.tick++;
  // Top yuvarlanır, yavaşlar, kenarlardan seker.
  if (fabsf(cat.bvx) > 0.03f) {
    cat.bx += cat.bvx;
    cat.bvx *= 0.965f;
    if (cat.bx < 8) {
      cat.bx = 8;
      cat.bvx = -cat.bvx * 0.6f;
    } else if (cat.bx > SCR_W - 8) {
      cat.bx = SCR_W - 8;
      cat.bvx = -cat.bvx * 0.6f;
    }
  } else {
    cat.bvx = 0;
  }
  switch (cat.st) {
    case CAT_SLEEP:
      if (!night) {
        cat.st = CAT_SIT;
        cat.until = ms + 3000;
      }
      break;
    case CAT_SIT:
      if (fabsf(cat.bvx) > 0.3f) cat.dir = cat.bx > cat.x ? 1 : -1;  // yuvarlanan topu izler
      if ((int32_t)(ms - cat.until) > 0) {
        if (night) {
          cat.st = CAT_SLEEP;
          break;
        }
        uint32_t r = rnd(100);
        if (r < 45) {
          cat.st = CAT_CHASE;
        } else if (r < 85) {
          cat.st = CAT_WALK;
          cat.target = 24 + rnd(272);
          cat.dir = cat.target > cat.x ? 1 : -1;
        } else {
          cat.until = ms + 2000 + rnd(4000);
        }
      }
      break;
    case CAT_WALK:
      cat.x += 0.7f * cat.dir;
      if ((cat.dir > 0 && cat.x >= cat.target) || (cat.dir < 0 && cat.x <= cat.target) || cat.x <= 22 || cat.x >= 298) {
        cat.st = CAT_SIT;
        cat.until = ms + 2000 + rnd(5000);
      }
      break;
    case CAT_CHASE: {
      float d = cat.bx - cat.x;
      cat.dir = d > 0 ? 1 : -1;
      if (fabsf(d) > 16) {
        cat.x += 1.8f * cat.dir;
      } else if (fabsf(cat.bvx) < 0.6f) {
        cat.st = CAT_BAT;
        cat.until = ms + 350;
      }
      break;
    }
    case CAT_BAT:
      if ((int32_t)(ms - cat.until) > 0) {
        cat.bvx = cat.dir * (2.2f + rnd(100) / 40.0f);  // patiyle iter
        if (rnd(100) < 65) {
          cat.st = CAT_CHASE;
        } else {
          cat.st = CAT_SIT;
          cat.until = ms + 1500 + rnd(3000);
        }
      }
      break;
    case CAT_HAPPY:
      cat.hop = ms - cat.happyAt < 160 ? 2 : (ms - cat.happyAt < 320 ? 4 : (ms - cat.happyAt < 480 ? 2 : 0));
      if ((int32_t)(ms - cat.until) > 0) {
        cat.st = CAT_SIT;
        cat.hop = 0;
        cat.until = ms + 2500;
      }
      break;
  }
  cat.x = constrain(cat.x, 22.f, 298.f);
}

static void catPet(int x) {
  cat.dir = x >= cat.x ? 1 : -1;
  cat.st = CAT_HAPPY;
  cat.happyAt = millis();
  cat.until = millis() + 1600;
}
