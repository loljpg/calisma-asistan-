#pragma once
// DAXEN VANE sistem katmanı · SD kart: kuyruk, çalışma defteri, plan önbelleği.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// SD kart: gönderilecek kayıtlar, çalışma defteri, plan önbelleği
// ============================================================================
// /calisma/kuyruk.txt      buluta henüz gitmemiş olaylar (satır başına bir JSON)
// /calisma/kayit-AAAA-AA.txt  cihazda yapılan her şeyin kalıcı kaydı
// /calisma/plan.json       son alınan plan; açılışta internet beklemeden gösterilir
#define SD_DIR "/calisma"
#define SD_QUEUE SD_DIR "/kuyruk.txt"
#define SD_QUEUE_TMP SD_DIR "/kuyruk.tmp"
#define SD_PLAN SD_DIR "/plan.json"
static const int QUEUE_MAX_SD = 3000, QUEUE_MAX_NVS = 40;
static int sdQueueCount = 0;
static SPIClass sdSPI(VSPI);

// Aynı anda açık dosya sınırı 5: biri varlık paketi (sürekli açık, ui/font.h), dördü günlük, yükleme,
// Lua ve uygulamalar için (her yuva yaklaşık 550 bayt bellek ayırır).
static bool sdBegin() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool ok = SD.begin(SD_CS, sdSPI, 10000000, "/sd", 5);
  if (!ok) {
    SD.end();
    ok = SD.begin(SD_CS, sdSPI, 4000000, "/sd", 5);
  }
  if (!ok || SD.cardType() == CARD_NONE) {
    SD.end();
    return false;
  }
  if (!SD.exists(SD_DIR)) SD.mkdir(SD_DIR);
  return true;
}

static int sdCountLines(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  int n = 0;
  uint8_t buf[256];
  size_t r;
  while ((r = f.read(buf, sizeof buf)) > 0)
    for (size_t i = 0; i < r; i++)
      if (buf[i] == '\n') n++;
  f.close();
  return n;
}

// Baştaki n satırı siler.
static void sdDropLines(int n) {
  File in = SD.open(SD_QUEUE, FILE_READ);
  if (!in) {
    sdQueueCount = 0;
    return;
  }
  File out = SD.open(SD_QUEUE_TMP, FILE_WRITE);
  int line = 0, kept = 0;
  while (in.available()) {
    String s = in.readStringUntil('\n');
    s.trim();
    if (!s.length()) continue;
    if (line++ < n) continue;
    if (out) {
      out.print(s);
      out.print('\n');
    }
    kept++;
  }
  in.close();
  if (out) out.close();
  SD.remove(SD_QUEUE);
  if (kept) SD.rename(SD_QUEUE_TMP, SD_QUEUE);
  else SD.remove(SD_QUEUE_TMP);
  sdQueueCount = kept;
}

static void queueAppend(const String& line) {
  Lock l;
  if (sdOk) {
    File f = SD.open(SD_QUEUE, FILE_APPEND);
    if (f && f.print(line + "\n") > 0) {
      f.close();
      if (++sdQueueCount > QUEUE_MAX_SD) sdDropLines(sdQueueCount - QUEUE_MAX_SD);
      return;
    }
    if (f) f.close();
    sdOk = false;  // kart çıkarıldı ya da bozuk: bundan sonra NVS
  }
  outbox.push_back(line);
  while ((int)outbox.size() > QUEUE_MAX_NVS) outbox.erase(outbox.begin());
  saveOutboxNvs();
}

static std::vector<String> queuePeek(int max) {
  Lock l;
  std::vector<String> out;
  if (!sdOk) {
    for (auto& e : outbox) {
      if ((int)out.size() >= max) break;
      out.push_back(e);
    }
    return out;
  }
  if (sdQueueCount <= 0) return out;
  File f = SD.open(SD_QUEUE, FILE_READ);
  while (f && f.available() && (int)out.size() < max) {
    String s = f.readStringUntil('\n');
    s.trim();
    if (s.length()) out.push_back(s);
  }
  if (f) f.close();
  if (out.empty()) sdQueueCount = 0;
  return out;
}

static void queueDrop(int n) {
  Lock l;
  if (sdOk) {
    sdDropLines(n);
    return;
  }
  outbox.erase(outbox.begin(), outbox.begin() + std::min((int)outbox.size(), n));
  saveOutboxNvs();
}

static int queueCount() {
  Lock l;
  return sdOk ? sdQueueCount : (int)outbox.size();
}

// Kart takılıysa NVS'de kalan kayıtlar SD'ye taşınır.
static void queueMigrate() {
  Lock l;
  sdQueueCount = sdCountLines(SD_QUEUE);
  if (outbox.empty()) return;
  File f = SD.open(SD_QUEUE, FILE_APPEND);
  if (!f) return;
  for (auto& e : outbox) {
    f.print(e);
    f.print('\n');
    sdQueueCount++;
  }
  f.close();
  outbox.clear();
  saveOutboxNvs();
}

// Gönderilmiş ama telefonun planına henüz yansımamış olaylar. Kart yeniden başlarsa ekranda
// kaybolmasınlar diye saklanır; telefonun planı bunları gösterince silinir.
static std::vector<String> sentLines;
#define SD_SENT SD_DIR "/gonderilen.txt"

static long long lineAt(const String& line) {
  int i = line.indexOf("\"at\":");
  return i < 0 ? 0 : atoll(line.c_str() + i + 5);
}

static void sentSave() {
  Lock l;
  String s;
  for (auto& e : sentLines) {
    s += e;
    s += '\n';
  }
  if (sdOk) {
    File f = SD.open(SD_SENT, FILE_WRITE);
    if (f) {
      f.print(s);
      f.close();
    }
    return;
  }
  prefs.begin("cs", false);
  if (s.length()) prefs.putBytes("sent", s.c_str(), s.length());
  else prefs.remove("sent");
  prefs.end();
}

static void sentLoad() {
  Lock l;
  String s;
  if (sdOk) {
    File f = SD.open(SD_SENT, FILE_READ);
    if (f) {
      s = f.readString();
      f.close();
    }
  } else {
    prefs.begin("cs", true);
    size_t n = prefs.getBytesLength("sent");
    if (n) {
      std::unique_ptr<char[]> b(new char[n + 1]);
      prefs.getBytes("sent", b.get(), n);
      b[n] = 0;
      s = b.get();
    }
    prefs.end();
  }
  sentLines.clear();
  int start = 0;
  while (start < (int)s.length()) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) nl = s.length();
    String line = s.substring(start, nl);
    line.trim();
    if (line.length()) sentLines.push_back(line);
    start = nl + 1;
  }
}

// Kuyruğun başındaki n olay gönderildi: kuyruktan çıkar, "gönderildi" listesine al.
static void queueDropSent(int n) {
  std::vector<String> head = queuePeek(n);
  {
    Lock l;
    for (auto& h : head) sentLines.push_back(h);
    while (sentLines.size() > 40) sentLines.erase(sentLines.begin());
  }
  queueDrop(n);
  sentSave();
}

static void sentPrune(long long updatedAt, const String& date) {
  bool changed = false;
  {
    Lock l;
    String tag = "\"date\":\"" + date + "\"";
    size_t before = sentLines.size();
    sentLines.erase(std::remove_if(sentLines.begin(), sentLines.end(),
                                   [&](const String& x) { return lineAt(x) <= updatedAt || x.indexOf(tag) < 0; }),
                    sentLines.end());
    changed = sentLines.size() != before;
  }
  if (changed) sentSave();
}

static void sdLog(const String& line, const String& date) {
  Lock l;
  if (!sdOk || date.length() < 7) return;
  File f = SD.open(String(SD_DIR "/kayit-") + date.substring(0, 7) + ".txt", FILE_APPEND);
  if (!f) return;
  f.print(line);
  f.print('\n');
  f.close();
}

static void cacheView(const String& raw) {
  Lock l;
  if (sdOk) {
    File f = SD.open(SD_PLAN, FILE_WRITE);
    if (f) {
      f.print(raw);
      f.close();
    }
    return;
  }
  if (raw.length() > 3800) return;
  prefs.begin("cs", false);
  prefs.putBytes("view", raw.c_str(), raw.length());
  prefs.end();
}

static String loadCachedView() {
  Lock l;
  String raw;
  if (sdOk) {
    File f = SD.open(SD_PLAN, FILE_READ);
    if (f) {
      raw = f.readString();
      f.close();
    }
    if (raw.length()) return raw;
  }
  prefs.begin("cs", true);
  size_t n = prefs.getBytesLength("view");
  if (n) {
    std::unique_ptr<char[]> buf(new char[n + 1]);
    prefs.getBytes("view", buf.get(), n);
    buf[n] = 0;
    raw = buf.get();
  }
  prefs.end();
  return raw;
}
