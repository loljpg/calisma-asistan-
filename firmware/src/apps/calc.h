#pragma once
// Hesap makinesi: bilimsel. Dört işlem, üs, kök, yüzde, parantez, sin/cos/tan (derece ya da radyan),
// ln, log, π, e ve son sonuç (Ans). Yazarken sonuç önizlenir; = ile sabitlenir.
#include "../os/shell.h"

static String calcExpr;
static double calcAns = 0;
static bool calcDeg = true, calcDone = false, calcErr = false;
static double calcResult = 0;
static bool calcHasResult = false;

// --- Ayrıştırıcı (özyinelemeli iniş) ---
struct CalcParser {
  const char* p;
  bool err = false;
  bool deg = true;
  double ans = 0;

  bool match(const char* s) {
    size_t n = strlen(s);
    if (strncmp(p, s, n) == 0) {
      p += n;
      return true;
    }
    return false;
  }
  bool startsPrimary() {
    return isdigit((uint8_t)*p) || *p == ',' || *p == '.' || *p == '(' || strncmp(p, "π", 2) == 0 || *p == 'e' ||
           strncmp(p, "Ans", 3) == 0 || strncmp(p, "√", 3) == 0 || strncmp(p, "sin", 3) == 0 || strncmp(p, "cos", 3) == 0 ||
           strncmp(p, "tan", 3) == 0 || strncmp(p, "ln", 2) == 0 || strncmp(p, "log", 3) == 0;
  }
  double number() {
    char buf[32];
    int n = 0;
    bool dot = false;
    while ((isdigit((uint8_t)*p) || ((*p == ',' || *p == '.') && !dot)) && n < 30) {
      if (*p == ',' || *p == '.') {
        dot = true;
        buf[n++] = '.';
      } else {
        buf[n++] = *p;
      }
      p++;
    }
    buf[n] = 0;
    return atof(buf);
  }
  double trigIn(double v) { return deg ? v * M_PI / 180.0 : v; }
  double primary() {
    if (isdigit((uint8_t)*p) || *p == ',' || *p == '.') return number();
    if (match("π")) return M_PI;
    if (match("Ans")) return ans;
    if (*p == 'e') {
      p++;
      return M_E;
    }
    if (match("(")) {
      double v = expr();
      if (!match(")") && *p) err = true;  // kapanmayan parantez sonda kabul edilir
      return v;
    }
    if (match("√")) {
      double v = unary();
      if (v < 0) err = true;
      return sqrt(v);
    }
    const char* fn[] = {"sin(", "cos(", "tan(", "ln(", "log("};
    for (int i = 0; i < 5; i++)
      if (match(fn[i])) {
        double v = expr();
        if (!match(")") && *p) err = true;
        switch (i) {
          case 0: return sin(trigIn(v));
          case 1: return cos(trigIn(v));
          case 2: {
            double c = cos(trigIn(v));
            if (fabs(c) < 1e-12) err = true;
            return tan(trigIn(v));
          }
          case 3:
            if (v <= 0) err = true;
            return log(v);
          default:
            if (v <= 0) err = true;
            return log10(v);
        }
      }
    err = true;
    return 0;
  }
  double postfix() {
    double v = primary();
    while (match("%")) v /= 100.0;
    return v;
  }
  double unary() {
    if (match("−") || match("-")) return -unary();
    if (match("+")) return unary();
    return power();
  }
  double power() {
    double b = postfix();
    if (match("^")) {
      double e = unary();  // sağdan birleşir
      double r = pow(b, e);
      if (std::isnan(r)) err = true;
      return r;
    }
    return b;
  }
  double term() {
    double v = unary();
    for (;;) {
      if (match("×") || match("*")) v *= unary();
      else if (match("÷") || match("/")) {
        double d = unary();
        if (d == 0) err = true;
        else v /= d;
      } else if (*p && startsPrimary()) {
        v *= unary();  // örtük çarpma: 2π, 3(4)
      } else {
        break;
      }
    }
    return v;
  }
  double expr() {
    double v = term();
    for (;;) {
      if (match("+")) v += term();
      else if (match("−") || match("-")) v -= term();
      else break;
    }
    return v;
  }
};

static bool calcEval(const String& s, double& out) {
  if (!s.length()) return false;
  CalcParser cp;
  cp.p = s.c_str();
  cp.deg = calcDeg;
  cp.ans = calcAns;
  out = cp.expr();
  if (*cp.p) cp.err = true;
  return !cp.err && std::isfinite(out);
}

static String calcFmt(double v) {
  if (fabs(v) < 1e-12) v = 0;
  char b[32];
  double a = fabs(v);
  if (a != 0 && (a >= 1e10 || a < 1e-6)) {
    snprintf(b, sizeof b, "%.6E", v);
    String s(b);
    int e = s.indexOf('E');
    String mant = s.substring(0, e), ex = s.substring(e);
    while (mant.endsWith("0")) mant.remove(mant.length() - 1);
    if (mant.endsWith(".")) mant.remove(mant.length() - 1);
    s = mant + ex;
    s.replace(".", ",");
    s.replace("-", "−");
    return s;
  }
  snprintf(b, sizeof b, "%.10g", v);
  String s(b);
  if (s.indexOf('e') >= 0) {
    snprintf(b, sizeof b, "%.10f", v);
    s = b;
    if (s.indexOf('.') >= 0) {
      while (s.endsWith("0")) s.remove(s.length() - 1);
      if (s.endsWith(".")) s.remove(s.length() - 1);
    }
  }
  s.replace(".", ",");
  s.replace("-", "−");
  return s;
}

static const char* const CALC_KEYS[5][6] = {
    {"sin", "cos", "tan", "(", ")", "C"},
    {"ln", "7", "8", "9", "÷", "⌫"},
    {"log", "4", "5", "6", "×", "^"},
    {"√", "1", "2", "3", "−", "%"},
    {"π", "0", ",", "Ans", "+", "="},
};
static const int CALC_KY = 92, CALC_KH = 29;

static void calcDrawKey(int r, int c, bool pressed) {
  int x = c * 320 / 6, w = (c + 1) * 320 / 6 - x, y = CALC_KY + r * CALC_KH;
  const char* k = CALC_KEYS[r][c];
  bool digit = isdigit((uint8_t)k[0]) || !strcmp(k, ",");
  bool eq = !strcmp(k, "=");
  uint16_t fill = pressed ? C_ACCBG : (digit ? C_SURF : C_SURF2);
  uint16_t border = pressed || eq ? C_ACC : C_LINE;
  tft.fillRect(x, y, w, CALC_KH, C_BG);
  panel(x + 1, y + 1, w - 2, CALC_KH - 2, fill, border);
  if (!strcmp(k, "⌫")) {
    drawIcon(I_BACK, x + w / 2 - 11, y + 6, 16, C_TEXT2, fill);
    tft.drawFastHLine(x + w / 2 - 3, y + 14, 10, C_TEXT2);
    return;
  }
  bool fn = strlen(k) > 1 && !(uint8_t)(k[0] & 0x80);
  uint16_t fg = digit ? C_TEXT : (!strcmp(k, "C") ? C_TEXT2 : (eq ? C_ACC : C_TEXT));
  text(k, x + w / 2, y + CALC_KH / 2 + 1, fn ? F_SM : F_MD, fg, fill, MC_DATUM);
}

static void calcDrawDisplay() {
  fillBg(0, HDR_H, SCR_W, CALC_KY - HDR_H);
  String e = calcExpr.length() ? calcExpr : String(calcDone ? "" : "0");
  // uzun ifadede baştan kırp
  String shown = e;
  while (textW(shown, F_MD) > 296 && shown.length()) shown = shown.substring(utf8Next(shown, 0));
  if (shown != e) shown = "…" + shown;
  text(shown, 308, HDR_H + 6, F_MD, calcDone ? C_TEXT3 : C_TEXT2, C_BG, TR_DATUM);
  if (calcErr) {
    text("Hata", 308, HDR_H + 34, F_MD, C_ACC, C_BG, TR_DATUM);
  } else if (calcHasResult) {
    String r = calcFmt(calcResult);
    const uint8_t* f = textW(r, F_LG) <= 296 ? F_LG : F_MD;
    text((calcDone ? "" : "= ") + r, 308, HDR_H + 30, f, calcDone ? C_TEXT : C_TEXT3, C_BG, TR_DATUM);
  }
  hline(0, CALC_KY - 3, SCR_W);
}

static void calcDrawModeBtn() {
  tft.fillRect(196, 0, 72, HDR_H - 1, C_BG);
  panel(204, 5, 60, 19, C_SURF, C_LINE);
  text(calcDeg ? "Derece" : "Radyan", 234, 15, F_XS, C_TEXT2, C_SURF, MC_DATUM);
}

static void calcDraw(bool full) {
  if (full) {
    header("Hesap makinesi");
    calcDrawModeBtn();
    fillBg(0, CALC_KY - 2, SCR_W, SCR_H - CALC_KY + 2);
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 6; c++) calcDrawKey(r, c, false);
  }
  calcDrawDisplay();
}

static void calcPreview() {
  double v;
  calcErr = false;
  calcHasResult = calcEval(calcExpr, v);
  if (calcHasResult) calcResult = v;
}

static void calcKey(const char* k) {
  bool isOp = !strcmp(k, "+") || !strcmp(k, "−") || !strcmp(k, "×") || !strcmp(k, "÷") || !strcmp(k, "^") || !strcmp(k, "%");
  if (!strcmp(k, "C")) {
    calcExpr = "";
    calcDone = calcErr = calcHasResult = false;
  } else if (!strcmp(k, "⌫")) {
    if (calcDone) {
      calcDone = false;
    } else {
      // işlev adları tek seferde silinir
      const char* fns[] = {"sin(", "cos(", "tan(", "log(", "ln(", "Ans"};
      bool done = false;
      for (auto f : fns)
        if (calcExpr.endsWith(f)) {
          calcExpr.remove(calcExpr.length() - strlen(f));
          done = true;
          break;
        }
      if (!done) calcExpr = utf8Pop(calcExpr);
    }
    calcPreview();
  } else if (!strcmp(k, "=")) {
    double v;
    if (calcEval(calcExpr, v)) {
      calcResult = v;
      calcAns = v;
      calcHasResult = true;
      calcErr = false;
      calcDone = true;
    } else if (calcExpr.length()) {
      calcErr = true;
      calcHasResult = false;
    }
  } else {
    if (calcDone) {  // yeni ifade; işlemle başlarsa sonuçtan devam eder
      calcExpr = isOp ? String("Ans") : String("");
      calcDone = false;
    }
    if (calcExpr.length() > 60) return;
    String add = k;
    if (!strcmp(k, "sin") || !strcmp(k, "cos") || !strcmp(k, "tan") || !strcmp(k, "ln") || !strcmp(k, "log")) add += "(";
    calcExpr += add;
    calcPreview();
  }
  calcDrawDisplay();
}

static void calcTap(int x, int y) {
  if (y < HDR_H) {
    if (x >= 196 && x < 268) {
      calcDeg = !calcDeg;
      calcDrawModeBtn();
      if (!calcDone) calcPreview();
      calcDrawDisplay();
    }
    return;
  }
  if (y < CALC_KY) return;
  int r = constrain((y - CALC_KY) / CALC_KH, 0, 4), c = constrain(x * 6 / 320, 0, 5);
  calcDrawKey(r, c, true);
  delay(50);
  calcDrawKey(r, c, false);
  calcKey(CALC_KEYS[r][c]);
}

static const App APP_CALC = {"Hesap makinesi", "Hesap Mak.", I_CALC, nullptr, calcDraw, calcTap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
