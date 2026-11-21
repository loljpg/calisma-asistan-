#pragma once
// DAXEN VANE sistem katmanı · Ayar yedeği: /daxen/yedek/ayarlar.json. Ekran, Pomodoro, ekran kilidi,
// hava konumu, dünya saatleri, bulut hesabı (adres, e-posta) ve kayıtlı Wi-Fi adları. Şifreler
// (Wi-Fi şifreleri, Web API anahtarı, bulut oturum anahtarı) yalnızca açık onayla girer. PIN,
// dokunmatik ayarı ve oyun kayıtları yedeğe girmez. Geri yüklemede yalnızca yedekte olanlar değişir.

__attribute__((noinline)) static String backupJson(bool secrets) {
  JsonDocument d;
  d["tur"] = "DAXEN VANE ayar yedeği";
  d["surum"] = 1;
  d["yazilim"] = "fw-" + String(FW_BUILD);
  d["sifreler"] = secrets;
  Lock l;
  JsonObject e = d["ekran"].to<JsonObject>();
  e["parlaklik"] = cfg.bright;
  e["gece"] = cfg.nightDim;
  e["otomatik"] = cfg.autoBright;
  e["kapak"] = cfg.coverTheme;
  e["ters"] = cfg.invert;
  d["pomodoro"] = cfg.pomodoro;
  d["kilit"] = cfg.lockOn;
  JsonObject h = d["hava"].to<JsonObject>();
  h["sehir"] = cfg.wCity;
  h["enlem"] = cfg.wLat;
  h["boylam"] = cfg.wLon;
  d["dunya"] = cfg.worldMask;
  JsonObject b = d["bulut"].to<JsonObject>();
  b["adres"] = cfg.dbUrl;
  b["eposta"] = cfg.email;
  if (secrets) {
    b["anahtar"] = cfg.apiKey;
    b["oturum"] = cfg.rtok;
  }
  JsonArray w = d["wifi"].to<JsonArray>();
  for (auto& k : wifiKnown) {
    JsonObject o = w.add<JsonObject>();
    o["ad"] = k.ssid;
    if (secrets) o["sifre"] = k.pass;
  }
  String s;
  serializeJson(d, s);
  return s;
}

// Yedeği uygular (kaydetmek çağıranda). false: yedek değil ya da bozuk
__attribute__((noinline)) static bool backupApply(const String& json, String& err) {
  JsonDocument d;
  if (deserializeJson(d, json) || String((const char*)(d["tur"] | "")) != "DAXEN VANE ayar yedeği") {
    err = "Bu bir DAXEN VANE ayar yedeği değil";
    return false;
  }
  Lock l;
  JsonObjectConst e = d["ekran"];
  if (!e.isNull()) {
    cfg.bright = constrain((int)(e["parlaklik"] | cfg.bright), 16, 255);
    cfg.nightDim = e["gece"] | cfg.nightDim;
    cfg.autoBright = e["otomatik"] | cfg.autoBright;
    cfg.coverTheme = e["kapak"] | cfg.coverTheme;
    cfg.invert = e["ters"] | cfg.invert;
  }
  cfg.pomodoro = d["pomodoro"] | cfg.pomodoro;
  cfg.lockOn = (d["kilit"] | cfg.lockOn) && authHasPin();  // PIN yoksa kilit açılmaz
  JsonObjectConst h = d["hava"];
  if (!h.isNull() && h["sehir"].is<const char*>()) {
    cfg.wCity = String((const char*)h["sehir"]);
    cfg.wLat = h["enlem"] | cfg.wLat;
    cfg.wLon = h["boylam"] | cfg.wLon;
  }
  cfg.worldMask = d["dunya"] | cfg.worldMask;
  JsonObjectConst b = d["bulut"];
  if (!b.isNull() && b["adres"].is<const char*>()) {
    String db = b["adres"], mail = b["eposta"] | "";
    if (db != cfg.dbUrl || mail != cfg.email) cfg.rtok = "", cfg.password = "";
    cfg.dbUrl = db;
    cfg.email = mail;
    if (b["anahtar"].is<const char*>()) cfg.apiKey = String((const char*)b["anahtar"]);
    if (b["oturum"].is<const char*>()) cfg.rtok = String((const char*)b["oturum"]);
  }
  JsonArrayConst w = d["wifi"];
  for (JsonObjectConst o : w) {
    String ssid = o["ad"] | "";
    if (!ssid.length()) continue;
    auto it = std::find_if(wifiKnown.begin(), wifiKnown.end(), [&](const WifiCred& c) { return c.ssid == ssid; });
    if (it == wifiKnown.end()) {
      if (wifiKnown.size() >= 5) continue;
      wifiKnown.push_back({ssid, ""});
      it = wifiKnown.end() - 1;
    }
    if (o["sifre"].is<const char*>()) it->pass = String((const char*)o["sifre"]);
  }
  return true;
}

__attribute__((noinline)) static bool backupSave(bool secrets, String& msg) {
  if (!sdOk) {
    msg = "SD kart takılı değil";
    return false;
  }
  if (!SD.exists("/daxen")) SD.mkdir("/daxen");
  if (!SD.exists(BACKUP_DIR)) SD.mkdir(BACKUP_DIR);
  String s = backupJson(secrets);
  File f = SD.open(BACKUP_FILE, FILE_WRITE);
  bool ok = f && f.print(s) == s.length();
  if (f) f.close();
  msg = ok ? String("Yedeklendi: " BACKUP_FILE) + (secrets ? " (şifreler dahil)" : "") : String("Yazılamadı");
  if (ok) sysLog("güvenlik", String("ayarlar yedeklendi") + (secrets ? " (şifreler dahil)" : ""));
  return ok;
}

__attribute__((noinline)) static bool backupLoad(String& msg) {
  File f = sdOk ? SD.open(BACKUP_FILE) : File();
  if (!f) {
    msg = "Yedek yok (" BACKUP_FILE ")";
    return false;
  }
  String s = f.readString();
  f.close();
  if (!backupApply(s, msg)) return false;
  saveConfig();
  wifiKnownSave();
  credsChanged = true;
  sysSetBacklight(cfg.bright);
  sysLog("güvenlik", "ayarlar yedekten geri yüklendi");
  msg = "Geri yüklendi";
  return true;
}
