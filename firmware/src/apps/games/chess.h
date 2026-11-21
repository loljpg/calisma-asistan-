#pragma once
// Satranç: DAXEN'e karşı. Bütün kurallar (rok, geçerken alma, terfi, şah, mat, pat).
// Motor: alfa-beta arama + taş alma devamı, malzeme ve konum tabloları. Üç zorluk.
#include "../../os/shell.h"

// ---------------------------------------------------------------------------
// Motor
// ---------------------------------------------------------------------------
enum { CP_P = 1, CP_N, CP_B, CP_R, CP_Q, CP_K };
struct ChPos {
  int8_t b[64];      // + beyaz, - siyah; satır 0 = 8. sıra (üst)
  int8_t side;       // 1 beyaz, -1 siyah
  uint8_t castle;    // 1 beyaz kısa, 2 beyaz uzun, 4 siyah kısa, 8 siyah uzun
  int8_t ep;         // geçerken alma karesi ya da -1
};
struct ChMove {
  uint8_t from, to;
  int8_t promo;  // 0 ya da taş türü
  uint8_t flags; // 1 alma, 2 geçerken, 4 rok
};

static const int CH_VAL[7] = {0, 100, 320, 330, 500, 900, 0};
// Konum tabloları (beyaz açısından, satır 0 = 8. sıra)
static const int8_t PST_P[64] = {0,  0,  0,  0,   0,   0,  0,  0,  50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10,
                                 5,  5,  10, 25,  25,  10, 5,  5,  0,  0,  0,  20, 20, 0,  0,  0,  5,  -5, -10, 0, 0, -10, -5, 5,
                                 5,  10, 10, -20, -20, 10, 10, 5,  0,  0,  0,  0,  0,  0,  0,  0};
static const int8_t PST_N[64] = {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   0,   0,   0,   -20, -40,
                                 -30, 0,   10,  15,  15,  10,  0,   -30, -30, 5,   15,  20,  20,  15,  5,   -30,
                                 -30, 0,   15,  20,  20,  15,  0,   -30, -30, 5,   10,  15,  15,  10,  5,   -30,
                                 -40, -20, 0,   5,   5,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50};
static const int8_t PST_B[64] = {-20, -10, -10, -10, -10, -10, -10, -20, -10, 0,   0,   0,   0,   0,   0,   -10,
                                 -10, 0,   5,   10,  10,  5,   0,   -10, -10, 5,   5,   10,  10,  5,   5,   -10,
                                 -10, 0,   10,  10,  10,  10,  0,   -10, -10, 10,  10,  10,  10,  10,  10,  -10,
                                 -10, 5,   0,   0,   0,   0,   5,   -10, -20, -10, -10, -10, -10, -10, -10, -20};
static const int8_t PST_K[64] = {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                                 -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                                 -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
                                 20,  20,  0,   0,   0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20};

static void chInit(ChPos& p) {
  const int8_t back[8] = {CP_R, CP_N, CP_B, CP_Q, CP_K, CP_B, CP_N, CP_R};
  memset(p.b, 0, 64);
  for (int c = 0; c < 8; c++) {
    p.b[c] = -back[c];
    p.b[8 + c] = -CP_P;
    p.b[48 + c] = CP_P;
    p.b[56 + c] = back[c];
  }
  p.side = 1;
  p.castle = 15;
  p.ep = -1;
}

static bool chAttacked(const ChPos& p, int sq, int by) {
  int r = sq / 8, c = sq % 8;
  // piyon
  int pr = r + (by == 1 ? 1 : -1);
  if (pr >= 0 && pr < 8) {
    if (c > 0 && p.b[pr * 8 + c - 1] == by * CP_P) return true;
    if (c < 7 && p.b[pr * 8 + c + 1] == by * CP_P) return true;
  }
  static const int8_t KN[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
  for (auto& d : KN) {
    int rr = r + d[0], cc = c + d[1];
    if (rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && p.b[rr * 8 + cc] == by * CP_N) return true;
  }
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      if (!dr && !dc) continue;
      int rr = r + dr, cc = c + dc;
      if (rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && p.b[rr * 8 + cc] == by * CP_K) return true;
      bool diag = dr && dc;
      for (int k = 1; k < 8; k++) {
        rr = r + dr * k;
        cc = c + dc * k;
        if (rr < 0 || rr > 7 || cc < 0 || cc > 7) break;
        int8_t q = p.b[rr * 8 + cc];
        if (!q) continue;
        if (q == by * CP_Q || q == by * (diag ? CP_B : CP_R)) return true;
        break;
      }
    }
  return false;
}

static int chKing(const ChPos& p, int side) {
  for (int i = 0; i < 64; i++)
    if (p.b[i] == side * CP_K) return i;
  return -1;
}
static bool chInCheck(const ChPos& p, int side) {
  int k = chKing(p, side);
  return k >= 0 && chAttacked(p, k, -side);
}

static void chPush(std::vector<ChMove>& v, int f, int t, uint8_t flags, int8_t promo = 0) { v.push_back(ChMove{(uint8_t)f, (uint8_t)t, promo, flags}); }
// Terfi: vezir önce (dokunarak oynayan oyuncuya vezir verilir), sonra at, kale, fil
static void chPushPromo(std::vector<ChMove>& v, int f, int t, uint8_t flags) {
  static const int8_t PR[4] = {CP_Q, CP_N, CP_R, CP_B};
  for (int8_t pr : PR) chPush(v, f, t, flags, pr);
}

static void chGen(const ChPos& p, std::vector<ChMove>& mv, bool capsOnly = false) {
  int s = p.side;
  for (int i = 0; i < 64; i++) {
    int8_t pc = p.b[i];
    if (pc * s <= 0) continue;
    int t = abs(pc), r = i / 8, c = i % 8;
    if (t == CP_P) {
      int dir = s == 1 ? -1 : 1, nr = r + dir;
      bool promoRank = nr == (s == 1 ? 0 : 7);
      if (nr >= 0 && nr < 8) {
        if (!capsOnly && !p.b[nr * 8 + c]) {
          if (promoRank) chPushPromo(mv, i, nr * 8 + c, 0);
          else {
            chPush(mv, i, nr * 8 + c, 0);
            int sr = s == 1 ? 6 : 1;
            if (r == sr && !p.b[(r + 2 * dir) * 8 + c]) chPush(mv, i, (r + 2 * dir) * 8 + c, 0);
          }
        }
        for (int dc = -1; dc <= 1; dc += 2) {
          int cc = c + dc;
          if (cc < 0 || cc > 7) continue;
          int to = nr * 8 + cc;
          if (p.b[to] * s < 0) {
            if (promoRank) chPushPromo(mv, i, to, 1);
            else chPush(mv, i, to, 1);
          }
          else if (to == p.ep) chPush(mv, i, to, 3);
        }
      }
    } else if (t == CP_N || t == CP_K) {
      static const int8_t KN[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
      static const int8_t KG[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
      const int8_t(*D)[2] = t == CP_N ? KN : KG;
      for (int k = 0; k < 8; k++) {
        int rr = r + D[k][0], cc = c + D[k][1];
        if (rr < 0 || rr > 7 || cc < 0 || cc > 7) continue;
        int to = rr * 8 + cc;
        if (p.b[to] * s > 0) continue;
        if (p.b[to]) chPush(mv, i, to, 1);
        else if (!capsOnly) chPush(mv, i, to, 0);
      }
      if (t == CP_K && !capsOnly) {  // rok
        int home = s == 1 ? 60 : 4;
        if (i == home && !chAttacked(p, i, -s)) {
          uint8_t ks = s == 1 ? 1 : 4, qs = s == 1 ? 2 : 8;
          if ((p.castle & ks) && !p.b[i + 1] && !p.b[i + 2] && p.b[i + 3] == s * CP_R && !chAttacked(p, i + 1, -s) && !chAttacked(p, i + 2, -s))
            chPush(mv, i, i + 2, 4);
          if ((p.castle & qs) && !p.b[i - 1] && !p.b[i - 2] && !p.b[i - 3] && p.b[i - 4] == s * CP_R && !chAttacked(p, i - 1, -s) &&
              !chAttacked(p, i - 2, -s))
            chPush(mv, i, i - 2, 4);
        }
      }
    } else {
      static const int8_t DIRS[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
      int d0 = t == CP_B ? 4 : 0, d1 = t == CP_R ? 4 : 8;
      for (int d = d0; d < d1; d++)
        for (int k = 1; k < 8; k++) {
          int rr = r + DIRS[d][0] * k, cc = c + DIRS[d][1] * k;
          if (rr < 0 || rr > 7 || cc < 0 || cc > 7) break;
          int to = rr * 8 + cc;
          if (p.b[to] * s > 0) break;
          if (p.b[to]) {
            chPush(mv, i, to, 1);
            break;
          }
          if (!capsOnly) chPush(mv, i, to, 0);
        }
    }
  }
}

static void chMake(const ChPos& p, const ChMove& m, ChPos& n) {
  n = p;
  int8_t pc = n.b[m.from];
  n.b[m.to] = m.promo ? (int8_t)(p.side * m.promo) : pc;
  n.b[m.from] = 0;
  if (m.flags & 2) n.b[m.to + (p.side == 1 ? 8 : -8)] = 0;  // geçerken alınan piyon
  if (m.flags & 4) {                                       // rok: kale de oynar
    if (m.to > m.from) {
      n.b[m.from + 1] = n.b[m.from + 3];
      n.b[m.from + 3] = 0;
    } else {
      n.b[m.from - 1] = n.b[m.from - 4];
      n.b[m.from - 4] = 0;
    }
  }
  n.ep = -1;
  if (abs(pc) == CP_P && abs(m.to - m.from) == 16) n.ep = (m.to + m.from) / 2;
  if (abs(pc) == CP_K) n.castle &= p.side == 1 ? ~3 : ~12;
  if (m.from == 63 || m.to == 63) n.castle &= ~1;
  if (m.from == 56 || m.to == 56) n.castle &= ~2;
  if (m.from == 7 || m.to == 7) n.castle &= ~4;
  if (m.from == 0 || m.to == 0) n.castle &= ~8;
  n.side = -p.side;
}

static void chLegal(const ChPos& p, std::vector<ChMove>& out) {
  std::vector<ChMove> mv;
  chGen(p, mv);
  out.clear();
  ChPos n;
  for (auto& m : mv) {
    chMake(p, m, n);
    if (!chInCheck(n, p.side)) out.push_back(m);
  }
}

static int chEval(const ChPos& p) {
  int s = 0;
  for (int i = 0; i < 64; i++) {
    int8_t pc = p.b[i];
    if (!pc) continue;
    int t = abs(pc), sq = pc > 0 ? i : (7 - i / 8) * 8 + i % 8;  // siyah için aynala
    int v = CH_VAL[t];
    if (t == CP_P) v += PST_P[sq];
    else if (t == CP_N) v += PST_N[sq];
    else if (t == CP_B || t == CP_Q) v += PST_B[sq] / (t == CP_Q ? 2 : 1);
    else if (t == CP_K) v += PST_K[sq];
    s += pc > 0 ? v : -v;
  }
  return s * p.side;
}

static uint32_t chNodes = 0, chNodeLimit = 30000;
static const int CH_MATE = 100000;
// Motor ayrı bir çekirdek sürecinde düşünür (os/kernel.h); arayüz bu arada donmaz.
static volatile bool chAiAbort = false;
static Proc* volatile chAiProcP = nullptr;
static inline bool chAiStopReq() { return chAiAbort || (chAiProcP && chAiProcP->killReq); }

static int chQuiesce(const ChPos& p, int alpha, int beta, int depth) {
  chNodes++;
  int stand = chEval(p);
  if (stand >= beta) return beta;
  if (alpha < stand) alpha = stand;
  if (depth <= 0 || chNodes > chNodeLimit * 2 || chAiStopReq()) return alpha;
  std::vector<ChMove> mv;
  chGen(p, mv, true);
  std::sort(mv.begin(), mv.end(), [&](const ChMove& a, const ChMove& b) { return abs(p.b[a.to]) * 10 - abs(p.b[a.from]) > abs(p.b[b.to]) * 10 - abs(p.b[b.from]); });
  ChPos n;
  for (auto& m : mv) {
    chMake(p, m, n);
    if (chInCheck(n, p.side)) continue;
    int sc = -chQuiesce(n, -beta, -alpha, depth - 1);
    if (sc >= beta) return beta;
    if (sc > alpha) alpha = sc;
  }
  return alpha;
}

static int chSearch(const ChPos& p, int depth, int alpha, int beta, int ply) {
  chNodes++;
  if (depth <= 0) return chQuiesce(p, alpha, beta, 4);
  std::vector<ChMove> mv;
  chGen(p, mv);
  std::sort(mv.begin(), mv.end(), [&](const ChMove& a, const ChMove& b) {
    int va = a.flags & 1 ? abs(p.b[a.to]) * 10 - abs(p.b[a.from]) + 100 : (a.promo ? 90 : 0);
    int vb = b.flags & 1 ? abs(p.b[b.to]) * 10 - abs(p.b[b.from]) + 100 : (b.promo ? 90 : 0);
    return va > vb;
  });
  bool any = false;
  ChPos n;
  for (auto& m : mv) {
    chMake(p, m, n);
    if (chInCheck(n, p.side)) continue;
    any = true;
    int sc = -chSearch(n, depth - 1, -beta, -alpha, ply + 1);
    if (sc >= beta) return beta;
    if (sc > alpha) alpha = sc;
    if (chNodes > chNodeLimit || chAiStopReq()) break;
  }
  if (!any) return chInCheck(p, p.side) ? -CH_MATE + ply : 0;
  return alpha;
}

// En iyi hamle (derinlik ve düğüm sınırıyla); kolayda biraz rastgelelik
static bool chBest(const ChPos& p, int depth, int noise, ChMove& best) {
  std::vector<ChMove> mv;
  chLegal(p, mv);
  if (mv.empty()) return false;
  for (size_t k = mv.size() - 1; k > 0; k--) std::swap(mv[k], mv[rnd(k + 1)]);
  int bestSc = -CH_MATE * 2;
  chNodes = 0;
  ChPos n;
  for (auto& m : mv) {
    chMake(p, m, n);
    int sc = -chSearch(n, depth - 1, -CH_MATE * 2, CH_MATE * 2, 1);
    if (noise) sc += (int)rnd(noise * 2 + 1) - noise;
    if (sc > bestSc) {
      bestSc = sc;
      best = m;
    }
    if (chAiStopReq()) break;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Arayüz
// ---------------------------------------------------------------------------
static ChPos chP;
static std::vector<ChPos> chHist;
static int chSel = -1, chLastFrom = -1, chLastTo = -1;
static uint8_t chLevel = 1;   // 0 kolay, 1 orta, 2 zor
static int8_t chHuman = 1;    // oyuncunun rengi
static bool chLoaded = false, chThinkReq = false;
static std::vector<ChMove> chMoves;
static String chMsg;
static const int CH_SQ = 26, CH_X = 6, CH_Y = HDR_H + 1;
static const char* const CH_LEVELS[] = {"Kolay", "Orta", "Zor"};

static void chSave() {
  uint8_t pack[70];
  memcpy(pack, chP.b, 64);
  pack[64] = chP.side == 1;
  pack[65] = chP.castle;
  pack[66] = (uint8_t)chP.ep;
  pack[67] = chLevel;
  pack[68] = chHuman == 1;
  pack[69] = 0xC5;
  Lock l;
  prefs.begin("chess", false);
  prefs.putBytes("p", pack, sizeof pack);
  prefs.end();
}

static void chAiCancel();
static void chNewGame() {
  chAiCancel();
  chInit(chP);
  chHist.clear();
  chSel = chLastFrom = chLastTo = -1;
  chMsg = "";
  chSave();
  chThinkReq = chHuman != chP.side;
}

static void chLoad() {
  uint8_t pack[70];
  size_t n;
  {
    Lock l;
    prefs.begin("chess", true);
    n = prefs.getBytes("p", pack, sizeof pack);
    prefs.end();
  }
  chLoaded = true;
  if (n != sizeof pack || pack[69] != 0xC5) {
    chNewGame();
    return;
  }
  memcpy(chP.b, pack, 64);
  chP.side = pack[64] ? 1 : -1;
  chP.castle = pack[65];
  chP.ep = (int8_t)pack[66];
  chLevel = pack[67] % 3;
  chHuman = pack[68] ? 1 : -1;
  chThinkReq = chHuman != chP.side;
}

// Ekrandaki kare (oyuncu siyahsa tahta döner)
static int chView(int sq) { return chHuman == 1 ? sq : 63 - sq; }

static void chFillPoly(const int* pts, int n, uint16_t col) {
  int miny = 999, maxy = -999;
  for (int i = 0; i < n; i++) {
    miny = std::min(miny, pts[i * 2 + 1]);
    maxy = std::max(maxy, pts[i * 2 + 1]);
  }
  for (int y = miny; y <= maxy; y++) {
    int xs[16], k = 0;
    for (int i = 0; i < n && k < 16; i++) {
      int x1 = pts[i * 2], y1 = pts[i * 2 + 1], x2 = pts[(i + 1) % n * 2], y2 = pts[(i + 1) % n * 2 + 1];
      if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) xs[k++] = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
    }
    std::sort(xs, xs + k);
    for (int i = 0; i + 1 < k; i += 2) tft.drawFastHLine(xs[i], y, xs[i + 1] - xs[i] + 1, col);
  }
}
static void chPoly(const int* pts, int n, uint16_t fill, uint16_t edge) {
  chFillPoly(pts, n, fill);
  for (int i = 0; i < n; i++) tft.drawLine(pts[i * 2], pts[i * 2 + 1], pts[(i + 1) % n * 2], pts[(i + 1) % n * 2 + 1], edge);
}

// Taşlar: sade geometrik siluetler (26 px kare)
static void chDrawPiece(int8_t pc, int x, int y) {
  if (!pc) return;
  bool white = pc > 0;
  uint16_t f = white ? C_TEXT : grayn(0x14), e = white ? grayn(0x10) : C_TEXT2;
  int cx = x + CH_SQ / 2, by = y + CH_SQ - 4;  // taban çizgisi
  auto base = [&](int w) {
    tft.fillRect(cx - w, by - 3, w * 2 + 1, 3, f);
    tft.drawRect(cx - w, by - 3, w * 2 + 1, 3, e);
  };
  switch (abs(pc)) {
    case CP_P: {
      int body[] = {cx - 5, by - 3, cx + 5, by - 3, cx + 2, by - 10, cx - 2, by - 10};
      chPoly(body, 4, f, e);
      tft.fillCircle(cx, by - 13, 4, f);
      tft.drawCircle(cx, by - 13, 4, e);
      base(6);
      break;
    }
    case CP_R: {
      int body[] = {cx - 5, by - 3, cx + 5, by - 3, cx + 4, by - 13, cx - 4, by - 13};
      chPoly(body, 4, f, e);
      int top[] = {cx - 6, by - 13, cx + 6, by - 13, cx + 6, by - 19, cx + 4, by - 19, cx + 4, by - 16, cx + 1, by - 16,
                   cx + 1, by - 19, cx - 1, by - 19, cx - 1, by - 16, cx - 4, by - 16, cx - 4, by - 19, cx - 6, by - 19};
      chPoly(top, 12, f, e);
      base(7);
      break;
    }
    case CP_N: {
      int head[] = {cx - 5, by - 3, cx + 6, by - 3, cx + 5, by - 12, cx + 2, by - 19, cx - 1, by - 17,
                    cx - 6, by - 14, cx - 7, by - 10, cx - 4, by - 9, cx - 1, by - 11, cx - 2, by - 7};
      chPoly(head, 10, f, e);
      tft.fillRect(cx, by - 15, 2, 2, e);  // göz
      base(7);
      break;
    }
    case CP_B: {
      int body[] = {cx - 5, by - 3, cx + 5, by - 3, cx + 2, by - 9, cx - 2, by - 9};
      chPoly(body, 4, f, e);
      int mitre[] = {cx, by - 20, cx + 5, by - 13, cx + 3, by - 9, cx - 3, by - 9, cx - 5, by - 13};
      chPoly(mitre, 5, f, e);
      tft.drawLine(cx + 1, by - 16, cx + 3, by - 12, e);
      base(6);
      break;
    }
    case CP_Q: {
      int body[] = {cx - 6, by - 3, cx + 6, by - 3, cx + 7, by - 16, cx + 3, by - 11, cx, by - 18, cx - 3, by - 11, cx - 7, by - 16};
      chPoly(body, 7, f, e);
      tft.fillCircle(cx, by - 19, 2, f);
      tft.drawCircle(cx, by - 19, 2, e);
      base(7);
      break;
    }
    case CP_K: {
      int body[] = {cx - 6, by - 3, cx + 6, by - 3, cx + 5, by - 13, cx - 5, by - 13};
      chPoly(body, 4, f, e);
      tft.fillRect(cx - 1, by - 21, 3, 8, f);
      tft.drawRect(cx - 1, by - 21, 3, 8, e);
      tft.fillRect(cx - 4, by - 19, 9, 3, f);
      tft.drawRect(cx - 4, by - 19, 9, 3, e);
      base(7);
      break;
    }
  }
}

static void chDrawSquare(int sq) {
  int v = chView(sq), r = v / 8, c = v % 8;
  int x = CH_X + c * CH_SQ, y = CH_Y + r * CH_SQ;
  bool light = (r + c) % 2 == 0;
  uint16_t bg = light ? rgbn(62, 66, 70) : rgbn(36, 39, 42);
  if (sq == chLastFrom || sq == chLastTo) bg = mix565(bg, C_ACC, 0.25f);
  if (sq == chSel) bg = C_ACCBG;
  tft.fillRect(x, y, CH_SQ, CH_SQ, bg);
  if (sq == chSel) tft.drawRect(x, y, CH_SQ, CH_SQ, C_ACC);
  chDrawPiece(chP.b[sq], x, y);
  if (chSel >= 0)
    for (auto& m : chMoves)
      if (m.from == chSel && m.to == sq) {
        if (chP.b[sq]) tft.drawRect(x + 1, y + 1, CH_SQ - 2, CH_SQ - 2, C_ACC);
        else tft.fillCircle(x + CH_SQ / 2, y + CH_SQ / 2, 3, C_ACC);
      }
}

static void chDrawBoard() {
  for (int i = 0; i < 64; i++) chDrawSquare(i);
}

static String chSqName(int sq) { return String((char)('a' + sq % 8)) + String(8 - sq / 8); }

static void chDrawSide() {
  fillBg(218, HDR_H, SCR_W - 218, SCR_H - HDR_H);
  bool myTurn = chP.side == chHuman;
  String st = chMsg.length() ? chMsg : (myTurn ? String("Sıra sende") : String("DAXEN düşünüyor…"));
  auto ls = wrapText(st, F_SM, 94, 3);
  for (size_t i = 0; i < ls.size(); i++) text(ls[i], 222, HDR_H + 8 + i * 18, F_SM, chMsg.length() ? C_ACC : C_TEXT, C_BG);
  if (chLastFrom >= 0) text(chSqName(chLastFrom) + " – " + chSqName(chLastTo), 222, HDR_H + 66, F_MD, C_TEXT2, C_BG);
  text(String("Sen: ") + (chHuman == 1 ? "Beyaz" : "Siyah"), 222, HDR_H + 92, F_XS, C_TEXT3, C_BG);
  button(222, HDR_H + 112, 92, 28, "Geri al", chHist.size() >= 2 ? B_NORMAL : B_DISABLED, I_UNDO);
  button(222, HDR_H + 146, 92, 28, CH_LEVELS[chLevel], B_NORMAL);
  button(222, HDR_H + 180, 92, 28, "Yeni oyun", B_PRIMARY);
}

static void chUpdateStatus() {
  chLegal(chP, chMoves);
  bool check = chInCheck(chP, chP.side);
  if (chMoves.empty()) chMsg = check ? (chP.side == chHuman ? "Mat! DAXEN kazandı" : "Mat! Kazandın") : "Pat: berabere";
  else chMsg = check ? "Şah!" : "";
}

static void chDraw(bool full) {
  if (!chLoaded) chLoad();
  if (full) {
    header("Satranç");
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  chUpdateStatus();
  chDrawBoard();
  chDrawSide();
}

static void chApply(const ChMove& m) {
  chHist.push_back(chP);
  ChPos n;
  chMake(chP, m, n);
  chP = n;
  chLastFrom = m.from;
  chLastTo = m.to;
  chSel = -1;
  chSave();
  chUpdateStatus();
  chDrawBoard();
  chDrawSide();
}

// --- Motor süreci ---
static ChPos chAiPos;
static ChMove chAiMove;
static int chAiDepth = 2, chAiNoise = 8, chAiPid = -1;
static volatile bool chAiBusy = false, chAiDone = false, chAiOk = false;
static uint8_t chDots = 0;
static uint32_t chDotsAt = 0;

static void chAiProc(Proc* pr) {
  chAiProcP = pr;
  uint32_t t0 = micros();
  ChMove m;
  bool ok = chBest(chAiPos, chAiDepth, chAiNoise, m);
  kAddBusy(pr, micros() - t0);
  chAiMove = m;
  chAiOk = ok && !chAiStopReq();
  chAiProcP = nullptr;
  chAiDone = true;
}

static void chAiCancel() {
  if (!chAiBusy) return;
  chAiAbort = true;
  kWait(chAiPid, 2000);
  chAiBusy = chAiDone = false;
}

static void chThink() {
  if (chP.side == chHuman || chAiBusy) return;
  chUpdateStatus();
  if (chMoves.empty()) return;
  chDrawSide();
  chAiPos = chP;
  chAiDepth = chLevel == 0 ? 1 : (chLevel == 1 ? 2 : 3);
  chAiNoise = chLevel == 0 ? 60 : 8;
  chNodeLimit = chLevel == 0 ? 4000 : (chLevel == 1 ? 20000 : 60000);
  chAiAbort = chAiDone = false;
  // Ayrı süreç (10 KB yığın) bellek bütçesine sığmıyorsa ya da açılamadıysa burada düşünülür
  chAiPid = memAsk(MJ_CHESS, 10 * 1024, 10 * 1024, nullptr) ? kSpawn("satranç", chAiProc, 10 * 1024, true) : -1;
  chAiBusy = chAiPid >= 0;
  if (chAiPid < 0) {
    chAiBusy = false;
    ChMove m;
    if (chBest(chP, chAiDepth, chAiNoise, m)) chApply(m);
  }
}

static void chTap(int x, int y) {
  if (y < HDR_H) return;
  if (x >= 218) {
    if (y >= HDR_H + 108 && y < HDR_H + 142 && chAiBusy && !chHist.empty()) {
      chAiCancel();  // DAXEN düşünürken: yalnızca senin son hamlen geri alınır
      chP = chHist.back();
      chHist.pop_back();
      chLastFrom = chLastTo = chSel = -1;
      chSave();
      chDraw(false);
    } else if (y >= HDR_H + 108 && y < HDR_H + 142 && chHist.size() >= 2) {
      chP = chHist[chHist.size() - 2];  // senin ve DAXEN'in son hamlesi
      chHist.resize(chHist.size() - 2);
      chLastFrom = chLastTo = chSel = -1;
      chSave();
      chDraw(false);
    } else if (y >= HDR_H + 142 && y < HDR_H + 176) {
      chLevel = (chLevel + 1) % 3;
      chSave();
      chDrawSide();
    } else if (y >= HDR_H + 176 && y < HDR_H + 212) {
      menu("Yeni oyun", {"Beyazla oyna", "Siyahla oyna"}, [](int c) {
        if (c < 0) return;
        chHuman = c == 0 ? 1 : -1;
        chNewGame();
      });
    }
    return;
  }
  if (chP.side != chHuman || chMoves.empty()) return;
  int c = (x - CH_X) / CH_SQ, r = (y - CH_Y) / CH_SQ;
  if (c < 0 || c > 7 || r < 0 || r > 7) return;
  int sq = chView(r * 8 + c);
  if (chSel >= 0) {
    for (auto& m : chMoves)
      if (m.from == chSel && m.to == sq) {
        chApply(m);
        chThinkReq = true;
        return;
      }
  }
  int old = chSel;
  chSel = chP.b[sq] * chHuman > 0 ? sq : -1;
  if (old != chSel) chDrawBoard();
}

static void chTick() {
  if (chAiBusy && chAiDone) {
    chAiBusy = chAiDone = false;
    if (chAiOk && chP.side != chHuman) chApply(chAiMove);
    else chDrawSide();
    return;
  }
  if (chAiBusy && millis() - chDotsAt > 300) {  // düşünme göstergesi
    chDotsAt = millis();
    chDots = (chDots + 1) % 3;
    for (int i = 0; i < 3; i++) tft.fillCircle(226 + i * 10, HDR_H + 54, 2, i == chDots ? C_ACC : C_TEXT3);
  }
  if (chThinkReq) {
    chThinkReq = false;
    chThink();
  }
}

static void chClose() { chAiCancel(); }

static const App APP_CHESS = {"Satranç", "Satranç", I_ROOK, nullptr, chDraw, chTap, nullptr, nullptr, nullptr, chTick, nullptr, chClose};
