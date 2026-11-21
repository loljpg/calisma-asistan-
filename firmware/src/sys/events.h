#pragma once
// DAXEN VANE sistem katmanı · Olaylar: plan, sayaç, kuyruk.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// Olaylar
// ============================================================================
// --- Veriyi ekrana hazırlama ---
static std::vector<Item> displayItems(int& studied, View& snapshot) {
  Lock l;
  snapshot = view;
  std::vector<Item> items = view.items;
  studied = view.studiedMin;
  for (auto& e : localEvents) {
    if (e.date != view.date || e.at <= view.updatedAt) continue;
    for (auto& it : items) {
      if (it.id != e.itemId) continue;
      if (e.type == "complete" && it.durum != "tamam") {
        it.durum = "tamam";
        studied += it.dk;
      } else if (e.type == "uncomplete" && it.durum == "tamam") {
        it.durum = "bekliyor";
        studied -= it.dk;
      } else if (e.type == "postpone" && it.durum != "tamam") {
        it.durum = "ertelendi";
      } else if (e.type == "session") {
        it.durum = "tamam";
      }
    }
    if (e.type == "session") studied += e.sec / 60;
  }
  return items;
}

static bool planIsToday(const View& v) { return v.valid && v.date.length() && v.date == todayStr(); }

static void enqueueEvent(const char* type, const String& itemId, int sec, const String& ders, const String& konu, int dk) {
  String date = todayStr();
  long long at = nowMs();
  JsonDocument d;
  d["type"] = type;
  if (itemId.length()) d["itemId"] = itemId;
  d["date"] = date;
  d["at"] = at;
  if (sec) d["sec"] = sec;
  if (ders.length()) d["ders"] = ders;
  if (konu.length()) d["konu"] = konu;
  String s;
  serializeJson(d, s);
  queueAppend(s);
  sdLog(s, date);
  {
    Lock l;
    LocalEvent e;
    e.type = type;
    e.itemId = itemId;
    e.date = date;
    e.sec = sec;
    e.dk = dk;
    e.at = at;
    localEvents.push_back(e);
    while (localEvents.size() > 60) localEvents.erase(localEvents.begin());
  }
}

// Soru sayacı (apps/yks.h): buluta ve SD'deki kalıcı kayda gider; plana etkisi yoktur, yerel olay
// listesine girmez (plan olaylarının yerini almasın).
static void enqueueSoru(const String& ders, int n) {
  String date = todayStr();
  JsonDocument d;
  d["type"] = "soru";
  d["date"] = date;
  d["at"] = nowMs();
  d["ders"] = ders;
  d["n"] = n;
  String s;
  serializeJson(d, s);
  queueAppend(s);
  sdLog(s, date);
}

// Açılışta kuyrukta bekleyen olaylar ekrana yeniden işlenir (telefon henüz görmedi).
static void rebuildLocalEvents() {
  std::vector<String> q = queuePeek(60);
  Lock l;
  for (auto& s : sentLines) q.push_back(s);
  localEvents.clear();
  for (auto& line : q) {
    JsonDocument d;
    if (deserializeJson(d, line)) continue;
    LocalEvent e;
    e.type = String((const char*)(d["type"] | ""));
    e.itemId = String((const char*)(d["itemId"] | ""));
    e.date = String((const char*)(d["date"] | ""));
    e.sec = d["sec"] | 0;
    e.at = d["at"].as<long long>();
    if (e.type.length() && e.type != "soru") localEvents.push_back(e);
  }
}

// Kronometrenin şimdiki hâli telefona gönderilmek üzere sıraya konur.
static void publishTimer() {
  long long now = nowMs();
  if (!now) return;  // saat ayarlanmadan duvar saati hesaplanamaz
  SharedTimer t;
  t.on = swRunning || swElapsedMs() > 0;
  if (t.on) {
    t.itemId = swItemId;
    t.date = todayStr();
    t.ders = swDers.length() ? swDers : String("Diğer");
    t.konu = swKonu;
    t.dk = swDk;
    if (swRunning) {
      t.startedAt = now - (long long)(millis() - swStart);
      t.acc = swAccMs;
    } else {
      t.startedAt = 0;
      t.acc = swElapsedMs();
    }
  }
  t.by = "device";
  Lock l;
  t.rev = std::max(now, timerRevSeen + 1);
  timerRevSeen = t.rev;
  outTimer = t;
  outTimerPending = true;
  bleTimerPending = true;
  remoteTimerDirty = false;
  timerSave(t);
}

static void resetTimer() {
  swRunning = false;
  swAccMs = 0;
  swItemId = swDers = swKonu = "";
  swDk = 0;
  pomoCount = 0;
  pomoBreak = false;
}

static void finishTimer() {
  uint32_t sec = swElapsedMs() / 1000;
  if (sec < 60) {
    resetTimer();
    publishTimer();
    uiTimerChanged();
    uiToast("1 dakikadan kısa süreler kaydedilmez");
    return;
  }
  if (!timeOk()) {
    uiToast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  enqueueEvent("session", swItemId, sec, swDers.length() ? swDers : String("Diğer"), swKonu, swDk);
  resetTimer();
  publishTimer();
  uiTimerChanged();
  uiToast(fmtMin(sec / 60) + " kaydedildi");
}

// Telefondan gelen kronometre (başlatma, duraklatma, bitirme) uygulanır.
static void applyRemoteTimer() {
  SharedTimer r;
  {
    Lock l;
    if (!remoteTimerDirty) return;
    if (remoteTimer.on && remoteTimer.startedAt > 0 && !timeOk()) return;  // saat gelince uygulanır
    r = remoteTimer;
    remoteTimerDirty = false;
  }
  if (r.by != "restore") timerSave(r);
  bool wasActive = swRunning || swElapsedMs() > 0;
  if (!r.on) {
    if (!wasActive) return;
    resetTimer();  // oturum telefonda kaydedildi ya da iptal edildi
    uiTimerChanged();
    if (r.by == "phone") uiToast("Kronometre telefonda kapatıldı");
    return;
  }
  long long now = nowMs();
  bool sameItem = wasActive && swItemId == r.itemId;
  swItemId = r.itemId;
  swDers = r.ders == "Diğer" ? String("") : r.ders;
  swKonu = r.konu;
  swDk = r.dk;
  if (!swDk && swItemId.length()) {
    Lock l;
    for (auto& it : view.items)
      if (it.id == swItemId) swDk = it.dk;
  }
  if (r.startedAt > 0 && now > 0) {
    long long ran = std::max(0LL, now - r.startedAt);
    swAccMs = (uint32_t)std::max(0LL, r.acc);
    swStart = millis() - (uint32_t)ran;  // taşma olsa da fark doğru kalır
    swRunning = true;
  } else {
    swRunning = false;
    swAccMs = (uint32_t)std::max(0LL, r.acc);
  }
  pomoCount = swElapsedMs() / 1000 / POMODORO_WORK_SEC;
  pomoBreak = false;
  uiTimerChanged();
  if (!sameItem && r.by == "phone") uiToast(swRunning ? "Kronometre telefonda başlatıldı" : "Kronometre telefondan geldi");
}

static void toggleItem(const Item& it) {
  if (!timeOk()) {
    uiToast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  bool done = it.durum == "tamam";
  if (!done && swItemId == it.id && swElapsedMs() > 0) {
    finishTimer();
    return;
  }
  enqueueEvent(done ? "uncomplete" : "complete", it.id, 0, "", "", it.dk);
  uiDataChanged();
  uiToast(done ? "Geri alındı" : "Tamamlandı");
}

static void postponeItem(const Item& it) {
  if (!timeOk()) {
    uiToast("Saat henüz ayarlanmadı, biraz bekle");
    return;
  }
  if (swItemId == it.id) {
    resetTimer();
    publishTimer();
  }
  enqueueEvent("postpone", it.id, 0, "", "", it.dk);
  uiDataChanged();
  uiToast("Ertelendi");
}

static void selectForTimer(const Item& it) {
  if (swElapsedMs() > 0 && swItemId != it.id) {
    uiToast("Önce çalışan kronometreyi bitir");
    return;
  }
  swItemId = it.id;
  swDers = it.ders;
  swKonu = it.konu;
  swDk = it.dk;
}

static void startItemTimer(const Item& it) {
  if (swElapsedMs() > 0 && swItemId != it.id) {
    uiToast("Önce çalışan kronometreyi bitir");
    return;
  }
  if (swItemId != it.id) {
    swAccMs = 0;
    pomoCount = 0;
    pomoBreak = false;
  }
  swItemId = it.id;
  swDers = it.ders;
  swKonu = it.konu;
  swDk = it.dk;
  if (!swRunning) {
    swStart = millis();
    swRunning = true;
  }
  publishTimer();
}

// Kronometre: başlat / duraklat (YKS → Sayaç)
static void timerToggleRun() {
  if (swRunning) {
    swAccMs = swElapsedMs();
    swRunning = false;
  } else {
    swStart = millis();
    swRunning = true;
  }
  publishTimer();
  uiTimerChanged();
}

static void timerReset() {
  bool was = swRunning || swElapsedMs() > 0;
  resetTimer();
  if (was) publishTimer();
  uiTimerChanged();
}

static void setPomodoro(bool on) {
  cfg.pomodoro = on;
  pomoCount = swElapsedMs() / 1000 / POMODORO_WORK_SEC;
  pomoBreak = false;
  saveConfig();
}
