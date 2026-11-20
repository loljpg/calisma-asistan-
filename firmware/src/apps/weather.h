#pragma once
// Hava durumu: şimdiki durum, 24 saatlik sıcaklık eğrisi (yağış olasılığıyla), 7 günlük tahmin.
// Şehir aranarak değiştirilebilir (Open-Meteo, anahtar gerekmez).
#include "../os/shell.h"

static void wxDrawCurrent(const Weather& w) {
  fillBg(0, HDR_H, SCR_W, 62);
  drawIcon(weatherIconId(w.code, w.isDay), 10, HDR_H + 8, 36, C_TEXT, C_BG);
  text(String((int)roundf(w.temp)) + "°", 56, HDR_H + 4, F_LG, C_TEXT, C_BG);
  text(fit(weatherText(w.code), F_SM, 130), 56, HDR_H + 36, F_SM, C_TEXT2, C_BG);
  text("Hissedilen " + String((int)roundf(w.feels)) + "°", 308, HDR_H + 3, F_XS, C_TEXT2, C_BG, TR_DATUM);
  text("Nem %" + String(w.hum), 308, HDR_H + 17, F_XS, C_TEXT2, C_BG, TR_DATUM);
  text("Rüzgâr " + String((int)roundf(w.wind)) + " km/s", 308, HDR_H + 31, F_XS, C_TEXT2, C_BG, TR_DATUM);
  String upd = weatherAt ? String((millis() - weatherAt) / 60000) + " dk önce" : String("");
  text(fit(cfg.wCity + (upd.length() ? " · " + upd : ""), F_XS, 150), 308, HDR_H + 45, F_XS, C_TEXT3, C_BG, TR_DATUM);
}

// 24 saatlik eğri: üstte sıcaklıklar, ortada çizgi (arkasında yağış olasılığı), altta saatler
static void wxDrawHourly(const Weather& w) {
  const int Y0 = HDR_H + 62, H = 60, X0 = 16, X1 = 304;
  fillBg(0, Y0, SCR_W, H);
  hline(0, Y0, SCR_W);
  if (w.hours < 2) return;
  int lo = 100, hi = -100;
  for (int i = 0; i < w.hours; i++) {
    lo = std::min(lo, (int)w.hTemp[i]);
    hi = std::max(hi, (int)w.hTemp[i]);
  }
  if (hi == lo) hi = lo + 1;
  const int gy0 = Y0 + 20, gh = 18, base = gy0 + gh + 4;
  auto px = [&](int i) { return X0 + (X1 - X0) * i / (w.hours - 1); };
  auto py = [&](int i) { return gy0 + gh - (w.hTemp[i] - lo) * gh / (hi - lo); };
  int bw = std::max(2, (X1 - X0) / w.hours - 3);
  for (int i = 0; i < w.hours; i++)
    if (w.hRain[i] > 10) {
      int bh = std::max(2, w.hRain[i] * (gh + 4) / 100);
      tft.fillRect(px(i) - bw / 2, base - bh, bw, bh, C_LINE);
    }
  for (int i = 1; i < w.hours; i++) tft.drawWideLine(px(i - 1), py(i - 1), px(i), py(i), 1.6f, C_ACC, C_BG);
  for (int i = 0; i < w.hours; i += 4) {
    tft.fillRect(px(i) - 1, py(i) - 1, 3, 3, C_TEXT);
    text(String(w.hTemp[i]) + "°", px(i), Y0 + 4, F_XS, C_TEXT2, C_BG, TC_DATUM);
    text(String((w.hHour0 + i) % 24), px(i), Y0 + 45, F_XS, C_TEXT3, C_BG, TC_DATUM);
  }
}

static void wxDrawDaily(const Weather& w) {
  const int Y0 = HDR_H + 122;
  fillBg(0, Y0, SCR_W, SCR_H - Y0);
  hline(0, Y0, SCR_W);
  int n = std::min(7, w.days);
  for (int i = 0; i < n; i++) {
    int cx = 23 + i * 45;
    String day = i == 0 ? String("Bugün") : String(GUN_KISA[w.dWday[i]]);
    text(day, cx, Y0 + 6, F_XS, i == 0 ? C_TEXT : C_TEXT2, C_BG, TC_DATUM);
    drawIcon(weatherIconId(w.dCode[i]), cx - 10, Y0 + 24, 20, w.dRain[i] >= 50 ? C_ACC : C_TEXT2, C_BG);
    text(String(w.dMax[i]) + "°", cx, Y0 + 48, F_SM, C_TEXT, C_BG, TC_DATUM);
    text(String(w.dMin[i]) + "°", cx, Y0 + 68, F_XS, C_TEXT3, C_BG, TC_DATUM);
  }
}

static void wxDraw(bool full) {
  if (full) {
    header("Hava durumu");
    headerAction(0, I_REFRESH);
    headerAction(1, I_SEARCH);
  }
  Weather w;
  {
    Lock l;
    w = weather;
  }
  if (!w.valid) {
    emptyState(HDR_H, SCR_H - HDR_H, I_CLOUD, "Hava durumu alınamadı",
               wifiConnected() ? String("Biraz sonra yeniden denenecek.") : String("Wi-Fi'ye bağlanınca gelir."));
    return;
  }
  wxDrawCurrent(w);
  wxDrawHourly(w);
  wxDrawDaily(w);
}

static void wxSearch() {
  textInput("Şehir ara", "", [](const String& q) {
    if (!wifiConnected()) {
      toast("Wi-Fi bağlantısı yok");
      return;
    }
    toast("Aranıyor…");
    static std::vector<GeoHit> hits;
    hits.clear();
    if (!weatherSearch(q, hits) || hits.empty()) {
      toast("Şehir bulunamadı");
      return;
    }
    std::vector<String> opts;
    for (auto& h : hits) opts.push_back(h.name + (h.region.length() ? ", " + h.region : String("")));
    menu("Şehir seç", opts, [](int c) {
      if (c < 0 || c >= (int)hits.size()) return;
      cfg.wCity = hits[c].name;
      cfg.wLat = hits[c].lat;
      cfg.wLon = hits[c].lon;
      saveConfig();
      {
        Lock l;
        weather.valid = false;
      }
      weatherRefresh();
      toast(cfg.wCity + " seçildi");
    });
  }, false, 40, "ör. İstanbul");
}

static void wxTap(int x, int y) {
  if (y >= HDR_H) return;
  int h = headerHit(x, y);
  if (h == 0) {
    weatherRefresh();
    toast("Yenileniyor…");
  } else if (h == 1) {
    wxSearch();
  }
}

static const App APP_WEATHER = {"Hava durumu", "Hava", I_PARTLY, nullptr, wxDraw, wxTap, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
