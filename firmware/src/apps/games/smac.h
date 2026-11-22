#pragma once
// Smaç: topu potadan potaya at. Parmağını geri çek ve bırak (sapan gibi): pota nişan yönüne döner,
// noktalar topun yolunu gösterir. Çembere değmeden giren atış "Mükemmel" sayılır, art arda geldikçe
// daha çok puan verir; üçüncüsünden sonra top alev alır. Duvardan sekip giren atış "Bant" (+1).
// Top geldiği potaya geri düşerse yeniden atılır; ekranın altına düşerse oyun biter. 8 sayıdan sonra
// bazı potalar yatay kayar. En iyi skor NVS'de ("smac").
// Çizim: ekranın değişen parçaları 64×64'lük ara bellekte (8 KB, yalnız oyun açıkken) baştan çizilip
// gönderilir, titreme olmaz. Ara bellek açılamazsa aynı çizim doğrudan ekrana (kırpma penceresiyle).
#include "../../os/shell.h"

static const float SM_G = 1150, SM_R = 8.5f, SM_RH = 22, SM_ND = 20, SM_NB = 12, SM_VMAX = 640;
static const int SM_TOP = HDR_H, SM_REST = 186, SM_T = 64, SM_DOTS = 14;
static const uint16_t SM_RIM = rgbn(232, 96, 60), SM_BALL = rgbn(238, 134, 52), SM_SEAM = rgbn(110, 48, 22),
                      SM_FIRE = rgbn(250, 76, 40);

struct SmHoop {
  bool used;
  float x, y, ang;  // dünya koordinatı (y aşağı), çemberin ortası; ang: nişan dönüşü (radyan)
  bool moving;
  float bx, amp, ph, sp;  // kayan pota: x = bx + amp·sin(ph)
};
struct SmRect {
  int x0, y0, x1, y1;
  bool ok;
};
enum SmState : uint8_t { SM_READY, SM_AIM, SM_FLY, SM_SETTLE, SM_SCROLL, SM_OVER };

static SmHoop smH[3];
static int smCur = 0, smNext = 1, smFrom = 0, smCaught = 0;  // topun potası, sıradaki, son atışın potası, tutan
static float smCam = 0, smCamTo = 0;           // ekran y = dünya y − smCam
static float smBx, smBy, smVx, smVy, smSpin, smSx, smSy;  // top; settle başlangıcı
static float smAx = 0, smAy = -1, smPow = 0;              // nişan yönü (birim) ve gücü
static float smDots[SM_DOTS][2];
static int smNDots = 0;
static SmState smSt = SM_READY;
static uint32_t smScore = 0, smBest = 0, smLast = 0, smLaunchAt = 0, smSetAt = 0;
static int smPerfect = 0;
static bool smRimHit = false, smWall = false, smScored = false, smLoaded = false, smStarted = false;
static int smTx, smTy;  // dokunmanın başladığı yer
static String smPopS;
static float smPopX, smPopY;
static uint32_t smPopUntil = 0;
static uint16_t smPopCol = C_TEXT;
static TFT_eSprite smSpr(&tft);
static bool smSprOk = false;
// nesnelerin bir önceki çizimdeki kutuları: 0-2 potalar, 3 top, 4.. noktalar, sonda yazı
static const int SM_NOBJ = 3 + 1 + SM_DOTS + 1;
static SmRect smPrev[SM_NOBJ];
static std::vector<SmRect> smDirty;

static float smSY(float y) { return y - smCam; }

// --- Çizim ---
static void smHoopPt(const SmHoop& h, float lx, float ly, float& x, float& y) {
  float c = cosf(h.ang), s = sinf(h.ang);
  x = h.x + lx * c - ly * s;
  y = smSY(h.y) + lx * s + ly * c;
}
static void smNet(TFT_eSPI& g, const SmHoop& h, int ox, int oy) {
  static const float P[][4] = {{-SM_RH + 2, 0, -SM_NB, SM_ND}, {SM_RH - 2, 0, SM_NB, SM_ND}, {-SM_NB, SM_ND, SM_NB, SM_ND},
                               {-SM_RH + 2, 0, SM_NB * 0.2f, SM_ND}, {SM_RH - 2, 0, -SM_NB * 0.2f, SM_ND},
                               {-SM_RH * 0.45f, 0, SM_NB, SM_ND * 0.6f}, {SM_RH * 0.45f, 0, -SM_NB, SM_ND * 0.6f}};
  for (auto& l : P) {
    float ax, ay, bx, by;
    smHoopPt(h, l[0], l[1], ax, ay);
    smHoopPt(h, l[2], l[3], bx, by);
    g.drawWideLine(ax - ox, ay - oy, bx - ox, by - oy, 1.2f, C_TEXT2, C_BG);
  }
}
static void smRim(TFT_eSPI& g, const SmHoop& h, int ox, int oy) {
  float ax, ay, bx, by;
  smHoopPt(h, -SM_RH, 0, ax, ay);
  smHoopPt(h, SM_RH, 0, bx, by);
  g.drawWideLine(ax - ox, ay - oy, bx - ox, by - oy, 3.6f, SM_RIM, C_BG);
}
static void smBall(TFT_eSPI& g, int ox, int oy) {
  float x = smBx - ox, y = smSY(smBy) - oy;
  bool fire = smPerfect >= 3;
  if (fire) g.fillSmoothCircle(lroundf(x), lroundf(y), 11, mix565(C_BG, SM_FIRE, 0.45f), C_BG);
  g.fillSmoothCircle(lroundf(x), lroundf(y), 8, fire ? SM_FIRE : SM_BALL, fire ? mix565(C_BG, SM_FIRE, 0.45f) : C_BG);
  float c = cosf(smSpin) * (SM_R - 1), s = sinf(smSpin) * (SM_R - 1);
  g.drawLine(lroundf(x - c), lroundf(y - s), lroundf(x + c), lroundf(y + s), SM_SEAM);
  g.drawLine(lroundf(x + s), lroundf(y - c), lroundf(x - s), lroundf(y + c), SM_SEAM);
}
static void smPop(TFT_eSPI& g, TFT_eSprite* sp, int ox, int oy) {
  if (!smPopS.length()) return;
  int x = lroundf(smPopX) - ox, y = lroundf(smSY(smPopY)) - oy;
  if (sp) fontDraw(*sp, F_SM, smPopS, x, y, smPopCol, C_BG, TC_DATUM);
  else fontDraw(tft, F_SM, smPopS, x, y, smPopCol, C_BG, TC_DATUM);
}
// Her şey (ox, oy kadar kaydırılmış): ağlar, top, çemberler, noktalar, yazı
static void smScene(TFT_eSPI& g, TFT_eSprite* sp, int ox, int oy) {
  for (auto& h : smH)
    if (h.used) smNet(g, h, ox, oy);
  smBall(g, ox, oy);
  for (auto& h : smH)
    if (h.used) smRim(g, h, ox, oy);
  for (int i = 0; i < smNDots; i++)
    g.fillCircle(lroundf(smDots[i][0]) - ox, lroundf(smSY(smDots[i][1])) - oy, i < 5 ? 2 : 1, i < 8 ? C_TEXT2 : C_TEXT3);
  smPop(g, sp, ox, oy);
}

// Ekranın bir dikdörtgenini baştan çizer (64×64'lük parçalarla)
static void smRender(const SmRect& r) {
  int x0 = std::max(r.x0, 0), y0 = std::max(r.y0, SM_TOP), x1 = std::min(r.x1, SCR_W), y1 = std::min(r.y1, SCR_H);
  if (x1 <= x0 || y1 <= y0) return;
  if (!smSprOk) {
    tft.setViewport(x0, y0, x1 - x0, y1 - y0, false);
    tft.fillRect(x0, y0, x1 - x0, y1 - y0, C_BG);
    smScene(tft, nullptr, 0, 0);
    tft.resetViewport();
    return;
  }
  for (int ty = y0; ty < y1; ty += SM_T)
    for (int tx = x0; tx < x1; tx += SM_T) {
      int tw = std::min(SM_T, x1 - tx), th = std::min(SM_T, y1 - ty);
      smSpr.fillRect(0, 0, tw, th, C_BG);
      smScene(smSpr, &smSpr, tx, ty);
      smSpr.pushSprite(tx, ty, 0, 0, tw, th);
    }
}

// Nesnelerin şimdiki kutuları; değişenlerin eski ve yeni kutusu yeniden çizilir
static void smBoxes(SmRect* b) {
  for (int i = 0; i < 3; i++) {
    const SmHoop& h = smH[i];
    int y = lroundf(smSY(h.y));
    b[i] = {(int)lroundf(h.x) - 30, y - 30, (int)lroundf(h.x) + 31, y + 31, h.used};
  }
  int y = lroundf(smSY(smBy));
  b[3] = {(int)lroundf(smBx) - 13, y - 13, (int)lroundf(smBx) + 14, y + 14, true};
  for (int i = 0; i < SM_DOTS; i++) {
    int dx = lroundf(smDots[i][0]), dy = lroundf(smSY(smDots[i][1]));
    b[4 + i] = {dx - 3, dy - 3, dx + 4, dy + 4, i < smNDots};
  }
  int w = smPopS.length() ? fontWidth(F_SM, smPopS.c_str()) : 0, py = lroundf(smSY(smPopY));
  b[SM_NOBJ - 1] = {(int)lroundf(smPopX) - w / 2 - 2, py - 2, (int)lroundf(smPopX) + w / 2 + 3, py + fontHeight(F_SM) + 2, w > 0};
}
static void smMark(const SmRect& r) {
  if (!r.ok) return;
  for (auto& d : smDirty)  // üst üste binenler birleşir
    if (r.x0 <= d.x1 + 4 && r.x1 >= d.x0 - 4 && r.y0 <= d.y1 + 4 && r.y1 >= d.y0 - 4) {
      d.x0 = std::min(d.x0, r.x0), d.y0 = std::min(d.y0, r.y0), d.x1 = std::max(d.x1, r.x1), d.y1 = std::max(d.y1, r.y1);
      return;
    }
  smDirty.push_back(r);
}
static void smFlush(bool all = false) {
  SmRect now[SM_NOBJ];
  smBoxes(now);
  smDirty.clear();
  if (all) {
    smDirty.push_back({0, SM_TOP, SCR_W, SCR_H, true});
  } else {
    for (int i = 0; i < SM_NOBJ; i++) {
      const SmRect &a = smPrev[i], &b = now[i];
      if (a.ok == b.ok && (!a.ok || (a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1))) continue;
      smMark(a);
      smMark(b);
    }
    if (smSt == SM_AIM || smSt == SM_FLY || smSt == SM_SETTLE) smMark(now[3]);  // top döner
    for (int pass = 0; pass < 2; pass++)  // birleşmeler yeni çakışmalar doğurabilir
      for (size_t i = 0; i < smDirty.size(); i++)
        for (size_t j = i + 1; j < smDirty.size(); j++) {
          SmRect &d = smDirty[i], &e = smDirty[j];
          if (e.x0 <= d.x1 && e.x1 >= d.x0 && e.y0 <= d.y1 && e.y1 >= d.y0) {
            d.x0 = std::min(d.x0, e.x0), d.y0 = std::min(d.y0, e.y0), d.x1 = std::max(d.x1, e.x1), d.y1 = std::max(d.y1, e.y1);
            smDirty.erase(smDirty.begin() + j--);
          }
        }
  }
  for (auto& d : smDirty) smRender(d);
  memcpy(smPrev, now, sizeof now);
}

static void smDrawScore() {
  tft.fillRect(120, 0, 150, HDR_H - 1, C_BG);
  text(String(smScore), 262, 15, F_MD, C_TEXT, C_BG, MR_DATUM);
  text("En iyi " + String(smBest), 226, 15, F_XS, C_TEXT3, C_BG, MR_DATUM);
}

static void smSetPop(const String& s, float x, float y, uint16_t col, uint32_t ms) {
  smPopS = s, smPopX = x, smPopY = y, smPopCol = col, smPopUntil = ms ? millis() + ms : 0;
}

// --- Oyun ---
static void smRestPos(const SmHoop& h, float& x, float& y) {
  float c = cosf(h.ang), s = sinf(h.ang);
  x = h.x - 7 * s;
  y = h.y + 7 * c;
}
static void smSpawn(int slot, const SmHoop& from) {
  SmHoop& h = smH[slot];
  h = SmHoop();
  h.used = true;
  h.x = from.x < SCR_W / 2 ? 180 + rnd(90) : 50 + rnd(90);
  h.y = from.y - 84 - rnd(22);
  if (smScore >= 8 && rnd(100) < 35) {
    h.moving = true;
    h.amp = 26 + rnd(20);
    h.bx = constrain(h.x, SM_RH + 12 + h.amp, SCR_W - SM_RH - 12 - h.amp);
    h.ph = rnd(628) / 100.f;
    h.sp = 1.1f + rnd(50) / 100.f;
    h.x = h.bx + h.amp * sinf(h.ph);
  }
}
static void smNew() {
  for (auto& h : smH) h.used = false;
  smH[0] = {true, 90, (float)SM_REST, 0, false, 0, 0, 0, 0};
  smCur = 0, smNext = 1, smFrom = 0;
  smCam = smCamTo = 0;
  smScore = 0, smPerfect = 0;
  smSpawn(1, smH[0]);
  smRestPos(smH[0], smBx, smBy);
  smVx = smVy = smSpin = 0;
  smNDots = 0;
  smSt = SM_READY;
  smSetPop(smStarted ? String("") : String("Geri çek, bırak"), SCR_W / 2, smCam + 132, C_TEXT3, 0);
  for (auto& p : smPrev) p.ok = false;
}

static void smSaveBest() {
  if (smScore <= smBest) return;
  smBest = smScore;
  Lock l;
  prefs.begin("smac", false);
  prefs.putUInt("best", smBest);
  prefs.end();
}

static void smDrawOver() {
  panel(70, 78, 180, 100, C_SURF, C_LINE);
  tft.fillRect(70, 78, 180, 2, SM_RIM);
  text("Oyun bitti", 160, 92, F_MD, C_TEXT, C_SURF, TC_DATUM);
  text("Skor " + String(smScore) + " · en iyi " + String(smBest), 160, 117, F_SM, C_TEXT2, C_SURF, TC_DATUM);
  button(100, 142, 120, 28, "Yeniden", B_PRIMARY, I_REFRESH, C_SURF);
}

// Nişan: yön, güç, topun geri çekilmesi, potanın dönmesi, yol noktaları
static void smAim(int x, int y) {
  float dx = smTx - x, dy = smTy - y, p = sqrtf(dx * dx + dy * dy);
  SmHoop& h = smH[smCur];
  if (p < 6 || dy / std::max(p, 1.f) > 0.3f) {  // çok kısa ya da aşağı: atış yok
    smPow = 0;
    h.ang = 0;
    smRestPos(h, smBx, smBy);
    smNDots = 0;
    return;
  }
  smAx = dx / p, smAy = dy / p, smPow = std::min(p, 90.f);
  h.ang = constrain(atan2f(smAx, -smAy), -1.2f, 1.2f);
  float rx, ry, pull = std::min(smPow * 0.12f, 9.f);
  smRestPos(h, rx, ry);
  smBx = rx - smAx * pull, smBy = ry - smAy * pull;
  float v = std::min(smPow * 7.4f, SM_VMAX), px = smBx, py = smBy, vx = smAx * v, vy = smAy * v;
  smNDots = 0;
  for (int i = 0; i < SM_DOTS * 4 && smNDots < SM_DOTS; i++) {  // 10 ms adım, her 4'te bir nokta
    vy += SM_G * 0.01f, px += vx * 0.01f, py += vy * 0.01f;
    if (px < SM_R) px = SM_R, vx = -vx * 0.85f;
    if (px > SCR_W - SM_R) px = SCR_W - SM_R, vx = -vx * 0.85f;
    if (smSY(py) > SCR_H) break;
    if (i % 4 == 3) smDots[smNDots][0] = px, smDots[smNDots][1] = py, smNDots++;
  }
}

static void smLaunch() {
  float v = std::min(smPow * 7.4f, SM_VMAX);
  smVx = smAx * v, smVy = smAy * v;
  smFrom = smCur;
  smRimHit = smWall = false;
  smNDots = 0;
  smLaunchAt = millis();
  smSt = SM_FLY;
  if (!smStarted) smStarted = true, smSetPop("", 0, 0, C_BG, 0);
}

static void smCatch(int slot) {
  SmHoop& h = smH[slot];
  h.moving = false;
  smCaught = slot;
  smScored = slot == smNext;
  if (smScored) {
    int pts = 1;
    String msg = "+1";
    if (!smRimHit) {
      smPerfect++;
      pts = 1 + std::min(smPerfect, 9);
      msg = "Mükemmel +" + String(pts);
    } else {
      smPerfect = 0;
    }
    if (smWall) {
      pts++;
      msg += " · Bant";
    }
    smScore += pts;
    smSetPop(msg, h.x, h.y - 46, smRimHit ? C_TEXT : SM_RIM, 900);
    smCur = slot;
    smDrawScore();
  } else {
    smPerfect = 0;
  }
  smSx = smBx, smSy = smBy;
  smVx = smVy = 0;
  smSetAt = millis();
  smSt = SM_SETTLE;
}

static void smOver() {
  smSt = SM_OVER;
  smSaveBest();
  smDrawScore();
  smDrawOver();
}

static void smStep(float dt) {
  smVy += SM_G * dt;
  float px = smBx, py = smBy;
  smBx += smVx * dt, smBy += smVy * dt;
  smSpin += smVx * dt * 0.08f;
  if (smBx < SM_R) smBx = SM_R, smVx = -smVx * 0.85f, smWall = true;
  if (smBx > SCR_W - SM_R) smBx = SCR_W - SM_R, smVx = -smVx * 0.85f, smWall = true;
  for (int i = 0; i < 3; i++) {
    SmHoop& h = smH[i];
    if (!h.used) continue;
    if (i == smFrom && millis() - smLaunchAt < 250) continue;  // atıldığı pota dönerken çarpmasın
    for (int e = -1; e <= 1; e += 2) {                       // çemberin iki ucu
      float ex = h.x + e * SM_RH * cosf(h.ang), ey = h.y + e * SM_RH * sinf(h.ang);
      float dx = smBx - ex, dy = smBy - ey, d = sqrtf(dx * dx + dy * dy), m = SM_R + 2;
      if (d >= m || d < 0.01f) continue;
      float nx = dx / d, ny = dy / d;
      smBx = ex + nx * m, smBy = ey + ny * m;
      float vn = smVx * nx + smVy * ny;
      if (vn < 0) smVx -= 1.5f * vn * nx, smVy -= 1.5f * vn * ny;
      smRimHit = true;
    }
    // çemberin içinden aşağı geçti: tuttu
    if (smVy > 0 && py < h.y && smBy >= h.y && fabsf(smBx - h.x) < SM_RH - 3 && fabsf(h.ang) < 0.05f) {
      smCatch(i);
      return;
    }
  }
}

static void smTick() {
  static uint32_t last = 0;
  uint32_t now = millis();
  float dt = std::min<uint32_t>(40, now - last) / 1000.f;
  last = now;
  if (!smLoaded || smSt == SM_OVER) return;
  for (int i = 0; i < 3; i++) {  // kayan potalar
    SmHoop& h = smH[i];
    if (!h.used || !h.moving) continue;
    h.ph += h.sp * dt;
    h.x = h.bx + h.amp * sinf(h.ph);
  }
  if (smPopUntil && now >= smPopUntil) smPopS = "", smPopUntil = 0;
  else if (smPopUntil) smPopY -= dt * 18;  // puan yazısı yükselir
  SmHoop& from = smH[smFrom];
  if (smSt != SM_AIM && from.ang != 0) {  // atıştan sonra pota yerine döner
    from.ang *= 0.6f;
    if (fabsf(from.ang) < 0.02f) from.ang = 0;
  }
  if (smSt == SM_FLY) {
    int n = std::max(1, (int)ceilf(dt / 0.004f));
    for (int i = 0; i < n && smSt == SM_FLY; i++) smStep(dt / n);
    if (smSt == SM_FLY && smSY(smBy) > SCR_H + 30 && smVy > 0) {
      smFlush();
      smOver();
      return;
    }
  } else if (smSt == SM_SETTLE) {
    float rx, ry, t = std::min(1.f, (now - smSetAt) / 140.f);
    smRestPos(smH[smCaught], rx, ry);
    smBx = smSx + (rx - smSx) * t, smBy = smSy + (ry - smSy) * t;
    if (t >= 1) {
      if (smScored) {  // sıradaki pota boş yuvaya; eski pota kayarak ekrandan çıkar
        int slot = -1;
        for (int i = 0; i < 3; i++)
          if (!smH[i].used) slot = i;
        if (slot < 0) slot = smFrom != smCur ? smFrom : (smCur + 1) % 3;
        smNext = slot;
        smSpawn(smNext, smH[smCur]);
        smCamTo = smH[smCur].y - SM_REST;
        smSt = SM_SCROLL;
      } else {
        smSt = SM_READY;
      }
    }
  } else if (smSt == SM_SCROLL) {
    float d = smCamTo - smCam;
    smCam += d * std::min(1.f, dt * 9);
    if (fabsf(d) < 0.6f) {
      smCam = smCamTo, smSt = SM_READY;
      for (int i = 0; i < 3; i++)  // ekranın altında kalan pota bırakılır
        if (smH[i].used && i != smCur && i != smNext && smSY(smH[i].y) > SCR_H + 20) smH[i].used = false;
    }
  }
  smFlush();
}

static void smDraw(bool full) {
  if (!smLoaded) {
    Lock l;
    prefs.begin("smac", true);
    smBest = prefs.getUInt("best", 0);
    prefs.end();
    smLoaded = true;
    smNew();
  }
  if (!smSprOk && sysHeapFree() > SM_T * SM_T * 2 + MEM_FLOOR + 4096 && sysHeapMaxAlloc() > SM_T * SM_T * 2 + 512) {
    smSpr.setColorDepth(16);
    smSprOk = smSpr.createSprite(SM_T, SM_T) != nullptr;
  }
  if (full) header("Smaç");
  smDrawScore();
  smFlush(true);
  if (smSt == SM_OVER) smDrawOver();
}

static bool smRaw(uint8_t ev, int x, int y) {
  if (ev == TE_DOWN && y < HDR_H) return false;
  if (smSt == SM_OVER) {
    if (ev == TE_DOWN && x >= 100 && x < 220 && y >= 138 && y < 174) {
      smNew();
      smDraw(false);
    }
    return true;
  }
  if (ev == TE_DOWN) {
    if (smSt != SM_READY) return true;
    smTx = x, smTy = y;
    smSt = SM_AIM;
    smPow = 0;
  } else if (smSt == SM_AIM) {
    smAim(x, y);
    if (ev == TE_UP) {
      if (smPow >= 14) {
        smLaunch();
      } else {  // iptal: top yerine
        smH[smCur].ang = 0;
        smRestPos(smH[smCur], smBx, smBy);
        smNDots = 0;
        smSt = SM_READY;
      }
    }
  }
  return true;
}

static void smClose() {
  if (smSt != SM_OVER && smScore) smSaveBest();
  smLoaded = false;
  if (smSprOk) smSpr.deleteSprite();
  smSprOk = false;
  std::vector<SmRect>().swap(smDirty);
}

static const App APP_SMAC = {"Smaç", "Smaç", I_BALL, nullptr, smDraw, nullptr, nullptr, nullptr, smRaw, smTick, nullptr, smClose};
