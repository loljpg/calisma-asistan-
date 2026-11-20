#pragma once
// Sistem çekirdeği ile arayüzün paylaştığı veri yapıları.
#include <Arduino.h>
#include <vector>

struct Item {
  String id, saat, ders, konu, durum;
  int dk = 0;
};

struct View {
  bool valid = false;
  String date;
  std::vector<Item> items;
  int studiedMin = 0, goalMin = 300, streak = 0, examDays = -1;
  long long updatedAt = 0;
};

// Gönderilmiş ama telefonun henüz işlemediği olaylar; ekranda hemen görünsün diye.
struct LocalEvent {
  String type, itemId, date;
  int sec = 0, dk = 0;
  long long at = 0;
};

// Open-Meteo: şimdiki durum, 24 saatlik ve 7 günlük tahmin
struct Weather {
  bool valid = false;
  float temp = 0, feels = 0, wind = 0;
  int code = 0, hum = 0;
  bool isDay = true;
  int days = 0;  // günlük tahmin sayısı (en fazla 7)
  int8_t dMax[7] = {}, dMin[7] = {};
  uint8_t dCode[7] = {}, dRain[7] = {}, dWday[7] = {};
  int hours = 0;  // saatlik tahmin sayısı (en fazla 24)
  int8_t hTemp[24] = {};
  uint8_t hCode[24] = {}, hRain[24] = {};
  uint8_t hHour0 = 0;  // ilk saatlik değerin saati
};

// Telefonla ortak kronometre: users/{uid}/device/timer (telefondaki logic.js sharedTimer ile aynı)
struct SharedTimer {
  bool on = false;
  String itemId, date, ders, konu, by;
  int dk = 0;
  long long startedAt = 0, acc = 0, rev = 0;  // startedAt: duvar saati (ms), duraklatıldıysa 0
};

struct Config {
  String dbUrl, apiKey, email, password;
  bool invert = DEFAULT_INVERT;
  bool calibrated = false, swapXY = false;
  int x1 = 400, x2 = 3480, y1 = 520, y2 = 3520;
  bool pomodoro = false;
  int theme = 0;           // eski tema seçimi (DAXEN tek, koyu temadır; yalnızca geriye uyum)
  bool coverTheme = false;  // çalan şarkının kapak rengi vurgu rengi olsun
  int bright = 255;         // ekran parlaklığı (16-255)
  bool nightDim = true;     // 22:00-07:00 arası ekran kısılır
  bool autoBright = false;  // ışık sensörüne göre parlaklık
  String wCity = WEATHER_CITY;
  float wLat = 40.78f, wLon = 30.40f;
  uint16_t worldMask = 0x0043;  // Dünya saatleri: seçili şehirler (bit)
};

struct Notif {
  String id, app, title, text;
  uint32_t icon = 0;
  uint32_t at = 0;  // millis
};

struct IconImg {
  uint32_t id = 0, used = 0;
  uint16_t px[ICON_PX * ICON_PX];
};

struct MediaState {
  bool on = false, playing = false, enabled = true;
  String app, title, artist;
  uint32_t pos = 0, dur = 0, at = 0;  // pos: at anındaki konum (ms)
  int vol = 0, volMax = 15;
  uint32_t art = 0;
};

struct WifiNet {
  String ssid;
  int rssi = 0;
  bool secure = true;
  bool known = false;
};

// Kayıtlı Wi-Fi ağları (en fazla 5)
struct WifiCred {
  String ssid, pass;
};
