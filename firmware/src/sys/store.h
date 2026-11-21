#pragma once
// DAXEN VANE sistem katmanı · Kalıcı ayarlar (NVS).
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// Kalıcı ayarlar
// ============================================================================
static void loadConfig() {
  Lock l;
  prefs.begin("cs", true);
  cfg.dbUrl = prefs.getString("db", "");
  cfg.apiKey = prefs.getString("key", "");
  cfg.email = prefs.getString("mail", "");
  cfg.password = prefs.getString("pass", "");
  cfg.invert = prefs.getBool("inv", DEFAULT_INVERT);
  cfg.calibrated = prefs.getBool("cal", false);
  cfg.swapXY = prefs.getBool("cswap", false);
  cfg.x1 = prefs.getInt("cx1", cfg.x1);
  cfg.x2 = prefs.getInt("cx2", cfg.x2);
  cfg.y1 = prefs.getInt("cy1", cfg.y1);
  cfg.y2 = prefs.getInt("cy2", cfg.y2);
  touchLoadCal(prefs);
  cfg.pomodoro = prefs.getBool("pomo", false);
  cfg.coverTheme = prefs.getBool("cover2", false);
  cfg.bright = constrain(prefs.getInt("bri", 255), 16, 255);
  cfg.nightDim = prefs.getBool("ndim", true);
  cfg.autoBright = prefs.getBool("abri", false);
  cfg.ldrInv = prefs.getBool("ldrinv", false);
  cfg.wCity = prefs.getString("wcity", WEATHER_CITY);
  cfg.wLat = prefs.getFloat("wlat", atof(WEATHER_LAT));
  cfg.wLon = prefs.getFloat("wlon", atof(WEATHER_LON));
  cfg.worldMask = prefs.getUShort("wmask", cfg.worldMask);
  // Kuyruk: fw-4 metin olarak ("outbox"), fw-5 bayt dizisi olarak ("ob2") saklar.
  String ob = prefs.getString("outbox", "");
  size_t n = prefs.getBytesLength("ob2");
  if (n) {
    std::unique_ptr<char[]> buf(new char[n + 1]);
    prefs.getBytes("ob2", buf.get(), n);
    buf[n] = 0;
    ob += buf.get();
  }
  prefs.end();
  outbox.clear();
  int start = 0;
  while (start < (int)ob.length()) {
    int nl = ob.indexOf('\n', start);
    if (nl < 0) nl = ob.length();
    String line = ob.substring(start, nl);
    line.trim();
    if (line.length()) outbox.push_back(line);
    start = nl + 1;
  }
}

static void saveConfig() {
  Lock l;
  prefs.begin("cs", false);
  prefs.putString("db", cfg.dbUrl);
  prefs.putString("key", cfg.apiKey);
  prefs.putString("mail", cfg.email);
  prefs.putString("pass", cfg.password);
  prefs.putBool("inv", cfg.invert);
  prefs.putBool("cal", cfg.calibrated);
  prefs.putBool("cswap", cfg.swapXY);
  prefs.putInt("cx1", cfg.x1);
  prefs.putInt("cx2", cfg.x2);
  prefs.putInt("cy1", cfg.y1);
  prefs.putInt("cy2", cfg.y2);
  touchStoreCal(prefs);
  prefs.putBool("pomo", cfg.pomodoro);
  prefs.putBool("cover2", cfg.coverTheme);
  prefs.putInt("bri", cfg.bright);
  prefs.putBool("ndim", cfg.nightDim);
  prefs.putBool("abri", cfg.autoBright);
  prefs.putBool("ldrinv", cfg.ldrInv);
  prefs.putString("wcity", cfg.wCity);
  prefs.putFloat("wlat", cfg.wLat);
  prefs.putFloat("wlon", cfg.wLon);
  prefs.putUShort("wmask", cfg.worldMask);
  prefs.end();
}

// SD kart yokken kuyruk NVS'de durur (en fazla 40 kayıt).
static void saveOutboxNvs() {
  Lock l;
  String s;
  for (auto& e : outbox) {
    s += e;
    s += '\n';
  }
  prefs.begin("cs", false);
  if (s.length()) prefs.putBytes("ob2", s.c_str(), s.length());
  else prefs.remove("ob2");
  prefs.remove("outbox");
  prefs.end();
}

static bool hasCreds() {
  Lock l;
  return cfg.dbUrl.length() && cfg.apiKey.length() && cfg.email.length() && cfg.password.length();
}

static void setNet(const String& msg, bool ok) {
  Lock l;
  if (netMsg != msg || cloudOk != ok) {
    netMsg = msg;
    cloudOk = ok;
    dataDirty = true;
  }
}
