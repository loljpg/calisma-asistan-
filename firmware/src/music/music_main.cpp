// DAXEN Müzik — kulaklıkla SD karttan müzik (ayrı yazılım)
//
// Ana yazılım (DAXEN VANE) Müzik uygulamasından bu yazılımı açar: SD karttaki muzik-<model>.bin'i
// boştaki güncelleme bölümüne yazar ve oradan başlar. Burada Wi-Fi ve telefon bağlantısı yoktur;
// bütün radyo Bluetooth kulaklığa ayrılır (klasik Bluetooth, A2DP). MP3'ler SD karttan çözülür.
// Çıkışta (ya da bir sorun olursa kendiliğinden) ana yazılıma dönülür; kaldığın yer saklanır.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <sys/time.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include "BluetoothA2DPSource.h"
#include "MP3DecoderHelix.h"

#include "../config.h"
#include "../core/types.h"
#include "../core/state.h"
#include "../core/util.h"
#include "../ui/theme.h"
#include "../ui/icons.h"
#include "../ui/widgets.h"
#include "../core/touch.h"

#define STR_(x) #x
#define STR(x) STR_(x)
// Ana yazılım SD'deki dosyanın hangi sürüm olduğunu bu işaretten anlar.
static const char MU_MARKER[] = "DXMU:" FW_VARIANT ":" STR(FW_BUILD) ";";

// Bu yazılım hiç "sağlam" işaretlenmez: çökerse ESP32 açılışta ana yazılıma döner.
extern "C" bool verifyRollbackLater() { return true; }

static void sysSetBacklight(int level) { ledcWrite(0, constrain(level, 4, 255)); }

// ============================================================================
// Şarkılar
// ============================================================================
static std::vector<String> tracks;
static int cur = -1;
static int volume = 60;          // 0-100
static bool shuffle = false;
static uint8_t repeatMode = 1;   // 0 kapalı, 1 liste, 2 tek şarkı
static String curTitle, curArtist, curFolder;

static bool isAudio(const String& n) {
  String e = extOf(n);
  return e == "mp3";
}

static void scanDir(const String& dir, int depth) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return;
  std::vector<String> sub;
  for (File f = d.openNextFile(); f && tracks.size() < 800; f = d.openNextFile()) {
    String n = baseName(f.name());
    if (!n.startsWith(".")) {
      String p = joinPath(dir, n);
      if (f.isDirectory()) {
        if (depth > 0 && n != "System Volume Information" && n != "daxen" && n != "calisma") sub.push_back(p);
      } else if (isAudio(n)) {
        tracks.push_back(p);
      }
    }
    f.close();
  }
  d.close();
  for (auto& s : sub) scanDir(s, depth - 1);
}

static void scanTracks() {
  tracks.clear();
  const char* dirs[] = {"/Muzik", "/Müzik", "/muzik", "/Music", "/music", "/MP3"};
  for (auto d : dirs)
    if (SD.exists(d)) scanDir(d, 3);
  if (tracks.empty()) scanDir("/", 2);
  std::sort(tracks.begin(), tracks.end(), [](const String& a, const String& b) { return trLower(a) < trLower(b); });
}

static String trackName(int i) {
  String n = baseName(tracks[i]);
  int d = n.lastIndexOf('.');
  return d > 0 ? n.substring(0, d) : n;
}

// --- ID3v2 etiketi: başlık ve sanatçı (Türkçe eski kodlama dahil) ---
static String latin5ToUtf8(const uint8_t* p, int n) {
  String o;
  for (int i = 0; i < n && p[i]; i++) {
    uint8_t c = p[i];
    if (c < 0x80) o += (char)c;
    else if (c == 0xD0) o += "Ğ";
    else if (c == 0xDD) o += "İ";
    else if (c == 0xDE) o += "Ş";
    else if (c == 0xF0) o += "ğ";
    else if (c == 0xFD) o += "ı";
    else if (c == 0xFE) o += "ş";
    else {
      o += (char)(0xC0 | (c >> 6));
      o += (char)(0x80 | (c & 0x3F));
    }
  }
  return o;
}
static String utf16ToUtf8(const uint8_t* p, int n, bool be) {
  String o;
  for (int i = 0; i + 1 < n; i += 2) {
    uint16_t u = be ? (p[i] << 8 | p[i + 1]) : (p[i] | p[i + 1] << 8);
    if (!u) break;
    if (u == 0xFEFF || u == 0xFFFE) continue;
    if (u < 0x80) o += (char)u;
    else if (u < 0x800) {
      o += (char)(0xC0 | (u >> 6));
      o += (char)(0x80 | (u & 0x3F));
    } else {
      o += (char)(0xE0 | (u >> 12));
      o += (char)(0x80 | ((u >> 6) & 0x3F));
      o += (char)(0x80 | (u & 0x3F));
    }
  }
  return o;
}
static String id3Text(const uint8_t* d, int n) {
  if (n < 2) return "";
  uint8_t enc = d[0];
  d++;
  n--;
  if (enc == 0) return latin5ToUtf8(d, n);
  if (enc == 3) {
    String o;
    for (int i = 0; i < n && d[i]; i++) o += (char)d[i];
    return o;
  }
  bool be = enc == 2;
  if (enc == 1 && n >= 2) be = d[0] == 0xFE;
  return utf16ToUtf8(d, n, be);
}

// Etiketi okur; sesin başladığı konumu döner
static uint32_t readId3(File& f, String& title, String& artist) {
  uint8_t h[10];
  f.seek(0);
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3)) {
    f.seek(0);
    return 0;
  }
  uint8_t ver = h[3];
  uint32_t size = ((h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) | ((h[8] & 0x7F) << 7) | (h[9] & 0x7F);
  uint32_t end = 10 + size, pos = 10;
  while (pos + 10 < end && pos < 64 * 1024) {
    uint8_t fh[10];
    f.seek(pos);
    if (f.read(fh, 10) != 10 || !fh[0]) break;
    uint32_t fs = ver >= 4 ? ((fh[4] & 0x7F) << 21) | ((fh[5] & 0x7F) << 14) | ((fh[6] & 0x7F) << 7) | (fh[7] & 0x7F)
                           : ((uint32_t)fh[4] << 24) | (fh[5] << 16) | (fh[6] << 8) | fh[7];
    bool isT = !memcmp(fh, "TIT2", 4), isA = !memcmp(fh, "TPE1", 4);
    if ((isT || isA) && fs > 1 && fs < 512) {
      uint8_t buf[512];
      f.read(buf, fs);
      String t = id3Text(buf, fs);
      t.trim();
      if (isT) title = t;
      else artist = t;
    }
    pos += 10 + fs;
  }
  f.seek(end);
  return end;
}

// ============================================================================
// Çalma: SD → MP3 çözücü → halka arabellek → Bluetooth (44,1 kHz stereo)
// ============================================================================
static const uint32_t RB_FRAMES = 8192;  // 32 KB ≈ 186 ms
static int16_t* rb = nullptr;
static volatile uint32_t rbHead = 0, rbTail = 0;
static inline uint32_t rbUsed() { return rbHead - rbTail; }
static inline uint32_t rbFree() { return RB_FRAMES - rbUsed(); }

static BluetoothA2DPSource a2dp;
static libhelix::MP3DecoderHelix* mp3 = nullptr;
static File file;
static volatile bool playing = false, wantPlay = false;
static volatile int cmdTrack = -1;     // çözücüye: bu şarkıyı aç
static volatile uint32_t cmdOffset = 0;
static volatile bool flushReq = false;
static volatile uint32_t playedFrames = 0;  // kulaklığa giden çerçeve
static uint32_t audioStart = 0, fileSize = 0;
static volatile uint32_t baseMs = 0;  // şarkının başından çalmaya kadar geçen (sürdürmede)
static volatile uint32_t durMs = 0;
static volatile int bitrate = 0;
static volatile bool newInfo = false;
static uint32_t rsAcc = 0;
static volatile bool trackEnded = false;
static volatile uint32_t filePos = 0;   // çözücünün okuduğu konum (kaydetmek için)
static volatile uint32_t cmdStart = 0;  // sesin başladığı konum (ID3 sonrası)

static void pcmOut(int16_t l, int16_t r) {
  while (rbFree() == 0) {
    if (flushReq || cmdTrack >= 0) return;
    vTaskDelay(2);
  }
  uint32_t i = (rbHead % RB_FRAMES) * 2;
  rb[i] = l;
  rb[i + 1] = r;
  rbHead++;
}

static void onPcm(MP3FrameInfo& info, short* pcm, size_t len, void*) {
  int ch = std::max(1, info.nChans);
  int rate = info.samprate > 0 ? info.samprate : 44100;
  if (info.bitrate > 0 && !bitrate) {
    bitrate = info.bitrate;
    if (fileSize > audioStart) durMs = (uint32_t)((uint64_t)(fileSize - audioStart) * 8000ULL / bitrate);
    if (cmdOffset > audioStart) baseMs = (uint32_t)((uint64_t)(cmdOffset - audioStart) * 8000ULL / bitrate);
    newInfo = true;
  }
  size_t frames = len / ch;
  for (size_t k = 0; k < frames; k++) {
    int16_t l = pcm[k * ch], r = ch > 1 ? pcm[k * ch + 1] : l;
    if (rate == 44100) {
      pcmOut(l, r);
    } else {  // basit hız uydurma
      rsAcc += 44100;
      while (rsAcc >= (uint32_t)rate) {
        pcmOut(l, r);
        rsAcc -= rate;
      }
    }
    if (flushReq || cmdTrack >= 0) return;
  }
}

static uint32_t posMs() {
  return baseMs + (uint32_t)((uint64_t)playedFrames * 1000ULL / 44100ULL);
}

// Bluetooth görevinden çağrılır: arabellekten okur, ses düzeyini uygular.
static int32_t a2dpData(Frame* f, int32_t count) {
  uint32_t avail = playing ? rbUsed() : 0;
  int32_t n = std::min<int32_t>(count, avail);
  int32_t g = volume * volume * 256 / 10000;  // kulak için karesel eğri
  for (int32_t i = 0; i < n; i++) {
    uint32_t k = (rbTail % RB_FRAMES) * 2;
    f[i].channel1 = (int16_t)((rb[k] * g) >> 8);
    f[i].channel2 = (int16_t)((rb[k + 1] * g) >> 8);
    rbTail++;
  }
  for (int32_t i = n; i < count; i++) f[i].channel1 = f[i].channel2 = 0;
  if (n > 0) playedFrames += n;
  return count;
}

static void decoderTask(void*) {
  static uint8_t buf[1024];
  for (;;) {
    if (cmdTrack >= 0) {
      int t = cmdTrack;
      uint32_t off = cmdOffset;
      cmdTrack = -1;
      if (file) file.close();
      mp3->end();
      rbTail = rbHead;  // eski sesi at
      playedFrames = 0;
      bitrate = 0;
      durMs = 0;
      baseMs = 0;
      rsAcc = 0;
      trackEnded = false;
      file = SD.open(tracks[t]);
      if (file) {
        audioStart = cmdStart;
        fileSize = file.size();
        uint32_t at = off > audioStart && off < fileSize ? off : audioStart;
        file.seek(at);
        filePos = at;
        cmdOffset = off > audioStart ? off : 0;
        mp3->begin();
      }
      newInfo = true;
      flushReq = false;
    }
    if (!playing || !file || trackEnded) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (rbFree() < 3000) {
      vTaskDelay(pdMS_TO_TICKS(4));
      continue;
    }
    int n = file.read(buf, sizeof buf);
    if (n > 0) filePos += n;
    if (n <= 0) {
      trackEnded = true;  // ana döngü sıradakini seçer
      continue;
    }
    mp3->write(buf, n);
  }
}

static void playTrack(int i, uint32_t offset = 0) {
  if (tracks.empty()) return;
  cur = constrain(i, 0, (int)tracks.size() - 1);
  curFolder = baseName(dirName(tracks[cur]));
  // Etiket arayüz tarafında okunur; çözücü yalnızca ses kısmını okur.
  String ti, ar;
  uint32_t start = 0;
  File f = SD.open(tracks[cur]);
  if (f) {
    start = readId3(f, ti, ar);
    f.close();
  }
  curTitle = ti.length() ? ti : trackName(cur);
  curArtist = ar;
  if (!ti.length() && !ar.length()) {  // etiket yoksa "Sanatçı - Şarkı.mp3" adından ayır
    int d = curTitle.indexOf(" - ");
    if (d > 0) {
      curArtist = curTitle.substring(0, d);
      curTitle = curTitle.substring(d + 3);
    }
  }
  cmdStart = start;
  cmdOffset = offset;
  flushReq = true;
  cmdTrack = cur;
  wantPlay = true;
}

static int nextIndex(int dir) {
  int n = tracks.size();
  if (!n) return -1;
  if (shuffle && n > 1) {
    int j;
    do j = rnd(n);
    while (j == cur);
    return j;
  }
  int j = cur + dir;
  if (j >= n) return repeatMode == 1 ? 0 : -1;
  if (j < 0) return repeatMode == 1 ? n - 1 : 0;
  return j;
}

// ============================================================================
// Bluetooth kulaklık
// ============================================================================
struct BtDev {
  String name;
  int rssi;
};
static std::vector<BtDev> btFound;
static String btTarget;  // seçilen kulaklık
static volatile bool btListChanged = false, btConnChanged = false;
static SemaphoreHandle_t btMtx;

static bool onBtFound(const char* ssid, esp_bd_addr_t, int rssi) {
  String s(ssid);
  if (s.length()) {
    xSemaphoreTake(btMtx, portMAX_DELAY);
    bool have = false;
    for (auto& d : btFound)
      if (d.name == s) {
        d.rssi = rssi;
        have = true;
      }
    if (!have && btFound.size() < 12) {
      btFound.push_back({s, rssi});
      btListChanged = true;
    }
    xSemaphoreGive(btMtx);
  }
  return btTarget.length() && s == btTarget;
}

static void onBtState(esp_a2d_connection_state_t, void*) { btConnChanged = true; }

static volatile uint8_t avrcKey = 0;
static void onAvrc(uint8_t key, bool released) {
  if (!released) avrcKey = key;
}

// ============================================================================
// Kalıcı durum (NVS "mus")
// ============================================================================
static void saveState() {
  Preferences p;
  p.begin("mus", false);
  if (cur >= 0 && cur < (int)tracks.size()) p.putString("path", tracks[cur]);
  p.putUInt("off", filePos);
  p.putInt("vol", volume);
  p.putBool("shf", shuffle);
  p.putUChar("rep", repeatMode);
  p.putString("hp", btTarget);
  p.end();
}

// Ana yazılıma dön
static void exitToMain() {
  playing = false;
  saveState();
  fillBg(0, 0, SCR_W, SCR_H);
  drawLogo(LOGO_L, LOGO_L_W, LOGO_L_H, (SCR_W - LOGO_L_W) / 2, 80);
  text("DAXEN VANE'e dönülüyor…", 160, 186, F_XS, C_TEXT3, C_BG, MC_DATUM);
  a2dp.end();
  delay(300);
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (other) esp_ota_set_boot_partition(other);
  esp_restart();
}

// ============================================================================
// Arayüz
// ============================================================================
enum MuScreen { MS_NOW, MS_LIB, MS_BT };
static MuScreen scr = MS_NOW;
static int libFirst = 0;
static const int LIB_ROW = 32, LIB_ROWS = 6;
static uint32_t lastTouch = 0;
static bool dimmed = false;

static bool btConnected() { return a2dp.is_connected(); }

static void drawHeaderNow() {
  header("Müzik");
  headerAction(0, I_HEADPHONES, btConnected());
  headerAction(1, I_LIST);
}

static void drawProgress() {
  fillBg(12, 120, 296, 24);
  uint32_t p = posMs(), d = durMs;
  progress(12, 122, 296, 2, d ? (int)((uint64_t)std::min(p, d) * 100 / d) : 0, C_ACC);
  text(fmtMs(p), 12, 130, F_XS, C_TEXT2, C_BG);
  if (d) text(fmtMs(d), 308, 130, F_XS, C_TEXT2, C_BG, TR_DATUM);
}

static void drawControls() {
  fillBg(0, 148, SCR_W, 46);
  iconButton(12, 152, 36, I_SHUFFLE, shuffle ? B_ON : B_NORMAL);
  iconButton(74, 150, 40, I_PREV);
  panel(124, 148, 72, 44, C_ACCBG, C_ACC);
  drawIcon(playing || wantPlay ? I_PAUSE : I_PLAY, 148, 158, 24, C_TEXT, C_ACCBG);
  iconButton(206, 150, 40, I_NEXT);
  iconButton(272, 152, 36, I_REPEAT, repeatMode ? B_ON : B_NORMAL);
  if (repeatMode == 2) text("1", 302, 154, F_XS, C_ACC, C_ACCBG, TR_DATUM);
}

static void drawVolume() {
  fillBg(0, 198, SCR_W, 42);
  drawIcon(volume ? I_VOLUME : I_SPEAKER_OFF, 12, 207, 20, C_TEXT2, C_BG);
  iconButton(40, 202, 30, I_MINUS);
  progress(80, 216, 156, 2, volume, C_TEXT);
  text(String(volume), 268, 210, F_XS, C_TEXT3, C_BG, TR_DATUM);
  iconButton(278, 202, 30, I_PLUS);
}

static void drawNowInfo() {
  fillBg(0, HDR_H, SCR_W, 88);
  panel(12, HDR_H + 8, 72, 72, C_SURF, C_LINE);
  drawIcon(I_NOTE, 28, HDR_H + 24, 40, playing ? C_ACC : C_TEXT3, C_SURF);
  if (cur < 0) {
    text(tracks.empty() ? "SD kartta şarkı yok" : "Bir şarkı seç", 96, HDR_H + 12, F_MD, C_TEXT, C_BG);
    text(tracks.empty() ? "MP3'leri Muzik klasörüne koy" : String(tracks.size()) + " şarkı", 96, HDR_H + 36, F_SM, C_TEXT2, C_BG);
    return;
  }
  bool conn = btConnected();
  auto ts = wrapText(curTitle, F_MD, 212, conn ? 2 : 1);
  int y = HDR_H + 8;
  for (auto& l : ts) {
    text(l, 96, y, F_MD, C_TEXT, C_BG);
    y += 22;
  }
  String sub = curArtist.length() ? curArtist : curFolder;
  if (sub.length()) text(fit(sub, F_SM, 212), 96, y + 2, F_SM, C_TEXT2, C_BG);
  if (!conn) {
    String bt = btTarget.length() ? "Bağlanıyor: " + btTarget : String("Kulaklık bağlı değil · dokun");
    text(fit(bt, F_XS, 212), 96, HDR_H + 66, F_XS, C_ACC, C_BG);
  }
}

static void drawNow(bool full) {
  if (full) {
    drawHeaderNow();
    fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  }
  drawNowInfo();
  drawProgress();
  drawControls();
  drawVolume();
}

static void drawLib(bool full) {
  if (full) header("Kitaplık · " + String(tracks.size()) + " şarkı");
  if (tracks.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_NOTE, "SD kartta şarkı yok",
               "MP3 dosyalarını Muzik klasörüne koy (DAXEN'deki Dosya aktarımı ile telefondan gönderebilirsin).");
    return;
  }
  int n = tracks.size();
  libFirst = constrain(libFirst, 0, std::max(0, n - LIB_ROWS));
  for (int r = 0; r < LIB_ROWS + 1; r++) {
    int i = libFirst + r, y = HDR_H + r * LIB_ROW;
    int h = std::min(LIB_ROW, SCR_H - y);
    fillBg(0, y, SCR_W, h);
    if (r >= LIB_ROWS || i >= n) continue;
    bool on = i == cur;
    if (on) tft.fillRect(0, y + 6, 3, LIB_ROW - 12, C_ACC);
    drawIcon(I_NOTE, 12, y + 7, 18, on ? C_ACC : C_TEXT3, C_BG);
    text(fit(trackName(i), F_SM, 220), 40, y + 9, F_SM, on ? C_TEXT : C_TEXT2, C_BG);
    String folder = baseName(dirName(tracks[i]));
    text(fit(folder, F_XS, 60), 308, y + 11, F_XS, C_TEXT3, C_BG, TR_DATUM);
    hline(40, y + LIB_ROW - 1, 268);
  }
  scrollBar(HDR_H, LIB_ROW * LIB_ROWS, libFirst, LIB_ROWS, n);
}

static void drawBt(bool full) {
  if (full) header("Kulaklık");
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  bool c = btConnected();
  drawIcon(I_HEADPHONES, 12, HDR_H + 10, 24, c ? C_ACC : C_TEXT3, C_BG);
  text(c ? (btTarget.length() ? btTarget : String("Bağlı")) : (btTarget.length() ? "Bağlanıyor: " + btTarget : String("Bağlı değil")), 46, HDR_H + 6,
       F_MD, C_TEXT, C_BG);
  text(c ? "Bağlı · ses buradan gidiyor" : "Kulaklığı eşleşme kipine al, listeden seç", 46, HDR_H + 28, F_XS, C_TEXT2, C_BG);
  sectionLabel(HDR_H + 46, "Bulunan cihazlar");
  std::vector<BtDev> list;
  xSemaphoreTake(btMtx, portMAX_DELAY);
  list = btFound;
  xSemaphoreGive(btMtx);
  for (int i = 0; i < (int)list.size() && i < 4; i++) {
    int y = HDR_H + 68 + i * 30;
    bool on = list[i].name == btTarget;
    drawIcon(I_HEADPHONES, 14, y + 6, 16, on ? C_ACC : C_TEXT2, C_BG);
    text(fit(list[i].name, F_SM, 210), 40, y + 15, F_SM, C_TEXT, C_BG, ML_DATUM);
    text(on ? (c ? "Bağlı" : "Seçili") : String(list[i].rssi) + " dBm", 308, y + 15, F_XS, on ? C_ACC : C_TEXT3, C_BG, MR_DATUM);
    hline(40, y + 29, 268);
  }
  if (list.empty()) text("Aranıyor…", 160, HDR_H + 110, F_SM, C_TEXT3, C_BG, MC_DATUM);
  button(12, 204, 296, 32, "Kulaklığı unut", btTarget.length() ? B_NORMAL : B_DISABLED, I_TRASH);
}

static void redraw(bool full) {
  if (scr == MS_NOW) drawNow(full);
  else if (scr == MS_LIB) drawLib(full);
  else drawBt(full);
}

static void go(MuScreen s) {
  scr = s;
  redraw(true);
}

static void togglePlay() {
  if (cur < 0) {
    if (!tracks.empty()) playTrack(0);
  } else {
    wantPlay = !wantPlay;
  }
  drawControls();
}

static void skip(int dir) {
  if (dir < 0 && posMs() > 3000) {
    playTrack(cur);
    return;
  }
  int j = nextIndex(dir);
  if (j >= 0) playTrack(j);
}

static void setVolume(int v) {
  volume = constrain(v, 0, 100);
  if (scr == MS_NOW) drawVolume();
}

static bool confirmExit = false;
static void drawExitDialog() {
  panel(20, 64, 280, 120, C_SURF, C_LINE);
  tft.fillRect(20, 64, 280, 2, C_ACC);
  text("Müzikten çıkılsın mı?", 34, 78, F_MD, C_TEXT, C_SURF);
  auto ls = wrapText("DAXEN VANE'e dönülür, kaldığın yer saklanır.", F_SM, 252, 2);
  for (size_t i = 0; i < ls.size(); i++) text(ls[i], 34, 104 + i * 18, F_SM, C_TEXT2, C_SURF);
  button(34, 142, 120, 32, "Vazgeç", B_NORMAL, I_NONE, C_SURF);
  button(166, 142, 120, 32, "Çık", B_PRIMARY, I_NONE, C_SURF);
}

static void onTap(int x, int y) {
  if (confirmExit) {
    if (y >= 136 && y < 182) {
      if (x >= 160) exitToMain();
      confirmExit = false;
      redraw(true);
    }
    return;
  }
  if (y < HDR_H) {
    int h = headerHit(x, y);
    if (h == -1) {
      if (scr == MS_NOW) {
        confirmExit = true;
        drawExitDialog();
      } else {
        go(MS_NOW);
      }
    } else if (scr == MS_NOW && h == 0) {
      go(MS_BT);
    } else if (scr == MS_NOW && h == 1) {
      if (cur >= 0) libFirst = std::max(0, cur - 2);
      go(MS_LIB);
    }
    return;
  }
  if (scr == MS_NOW) {
    if (y >= 146 && y < 196) {
      if (x < 60) {
        shuffle = !shuffle;
        drawControls();
      } else if (x < 120) skip(-1);
      else if (x < 200) togglePlay();
      else if (x < 260) skip(1);
      else {
        repeatMode = (repeatMode + 1) % 3;
        drawControls();
      }
    } else if (y >= 198) {
      if (x < 76) setVolume(volume - 5);
      else if (x >= 270) setVolume(volume + 5);
      else setVolume((x - 80) * 100 / 156);
    } else if (y >= HDR_H + 60 && y < HDR_H + 84 && !btConnected()) {
      go(MS_BT);
    }
  } else if (scr == MS_LIB) {
    int r = (y - HDR_H) / LIB_ROW, i = libFirst + r;
    if (r < LIB_ROWS && i < (int)tracks.size()) {
      playTrack(i);
      go(MS_NOW);
    }
  } else {
    if (y >= 200) {
      btTarget = "";
      a2dp.clean_last_connection();
      a2dp.disconnect();
      saveState();
      drawBt(false);
      return;
    }
    int i = (y - HDR_H - 68) / 30;
    std::vector<BtDev> list;
    xSemaphoreTake(btMtx, portMAX_DELAY);
    list = btFound;
    xSemaphoreGive(btMtx);
    if (y >= HDR_H + 68 && i >= 0 && i < (int)list.size() && i < 4 && list[i].name != btTarget) {
      btTarget = list[i].name;  // bir sonraki aramada bulununca bağlanır
      saveState();
      if (btConnected()) {
        a2dp.clean_last_connection();
        a2dp.disconnect();
      }
      drawBt(false);
    }
  }
}

static void onSwipe(int dir) {
  if (scr == MS_LIB && (dir == 2 || dir == 3)) {
    int n = tracks.size();
    libFirst = constrain(libFirst + (dir == 2 ? LIB_ROWS - 1 : -(LIB_ROWS - 1)), 0, std::max(0, n - LIB_ROWS));
    drawLib(false);
  } else if (scr == MS_NOW && dir == 0) {
    skip(1);
  } else if (scr == MS_NOW && dir == 1) {
    skip(-1);
  } else if (scr != MS_NOW && dir == 1) {
    go(MS_NOW);
  }
}

// Dokunma: bırakınca dokunuş ya da kaydırma
static void handleTouch() {
  static bool down = false;
  static int sx, sy, lx, ly;
  static uint32_t lastSeen = 0;
  int x, y;
  bool t = getTouch(x, y);
  uint32_t ms = millis();
  if (t) {
    lastSeen = ms;
    lastTouch = ms;
    if (!down) {
      down = true;
      sx = lx = x;
      sy = ly = y;
      if (dimmed) {
        dimmed = false;
        sysSetBacklight(cfg.bright);
        sx = -1000;  // yalnızca ekranı açar
      }
    } else {
      lx = (lx + x) / 2;
      ly = (ly + y) / 2;
    }
    return;
  }
  if (!down || ms - lastSeen < 40) return;
  down = false;
  if (sx < -100) return;
  int dx = lx - sx, dy = ly - sy;
  if (abs(dx) < 14 && abs(dy) < 14) onTap(sx, sy);
  else if (abs(dx) > 40 && abs(dx) > abs(dy)) onSwipe(dx < 0 ? 0 : 1);
  else if (abs(dy) > 30) onSwipe(dy < 0 ? 2 : 3);
}

// ============================================================================
// Başlangıç ve döngü
// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println(MU_MARKER);
  mtx = xSemaphoreCreateRecursiveMutex();
  btMtx = xSemaphoreCreateMutex();
  pinMode(PIN_BOOT, INPUT_PULLUP);
  // LED'ler kapalı
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, HIGH);

  // Ortak ayarlar (dokunmatik ayarı, parlaklık) ana yazılımın kayıtlarından okunur.
  prefs.begin("cs", true);
  cfg.invert = prefs.getBool("inv", DEFAULT_INVERT);
  cfg.swapXY = prefs.getBool("cswap", false);
  cfg.x1 = prefs.getInt("cx1", cfg.x1);
  cfg.x2 = prefs.getInt("cx2", cfg.x2);
  cfg.y1 = prefs.getInt("cy1", cfg.y1);
  cfg.y2 = prefs.getInt("cy2", cfg.y2);
  cfg.bright = constrain(prefs.getInt("bri", 255), 16, 255);
  prefs.end();
  themeApply();
  setenv("TZ", "<+03>-3", 1);
  tzset();

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(cfg.invert);
  tft.setSwapBytes(true);
  fillBg(0, 0, SCR_W, SCR_H);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  sysSetBacklight(cfg.bright);
  tsBegin();
  spr.setColorDepth(16);
  sprOk = spr.createSprite(SPR_W, SPR_H) != nullptr;
  if (sprOk) spr.loadFont(F_XL);

  drawLogo(LOGO_L, LOGO_L_W, LOGO_L_H, (SCR_W - LOGO_L_W) / 2, 74);
  text("Müzik", 160, 168, F_SM, C_TEXT2, C_BG, MC_DATUM);
  text("SD kart okunuyor…", 160, 196, F_XS, C_TEXT3, C_BG, MC_DATUM);

  static SPIClass sdSPI(VSPI);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS, sdSPI, 25000000);
  if (sdOk) scanTracks();

  rb = (int16_t*)malloc(RB_FRAMES * 4);
  mp3 = new libhelix::MP3DecoderHelix(onPcm);

  Preferences p;
  p.begin("mus", true);
  String last = p.getString("path", "");
  uint32_t off = p.getUInt("off", 0);
  volume = constrain(p.getInt("vol", 60), 0, 100);
  shuffle = p.getBool("shf", false);
  repeatMode = p.getUChar("rep", 1) % 3;
  btTarget = p.getString("hp", "");
  p.end();

  xTaskCreatePinnedToCore(decoderTask, "mp3", 8192, nullptr, 2, nullptr, 1);
  for (int i = 0; i < (int)tracks.size(); i++)
    if (tracks[i] == last) {
      playTrack(i, off);
      wantPlay = false;  // kulaklık bağlanınca kullanıcı başlatır
      break;
    }

  a2dp.set_local_name("DAXEN VANE");
  a2dp.set_ssid_callback(onBtFound);
  a2dp.set_auto_reconnect(true);
  a2dp.set_on_connection_state_changed(onBtState);
  a2dp.set_avrc_passthru_command_callback(onAvrc);
  a2dp.start(a2dpData);

  lastTouch = millis();
  redraw(true);
}

void loop() {
  handleTouch();
  uint32_t ms = millis();

  // BOOT: çal / duraklat
  static bool bootDown = false;
  bool b = digitalRead(PIN_BOOT) == LOW;
  if (b && !bootDown) togglePlay();
  bootDown = b;

  // Kulaklık tuşları
  uint8_t k = avrcKey;
  if (k) {
    avrcKey = 0;
    if (k == 0x44 || k == 0x46 || k == 0x45) togglePlay();
    else if (k == 0x4B) skip(1);
    else if (k == 0x4C) skip(-1);
    else if (k == 0x41) setVolume(volume + 5);
    else if (k == 0x42) setVolume(volume - 5);
  }

  // Çalma yalnızca kulaklık bağlıyken ilerler
  bool should = wantPlay && btConnected() && cur >= 0;
  if (should != playing) {
    playing = should;
    if (scr == MS_NOW && !confirmExit) drawControls();
  }
  if (trackEnded && cmdTrack < 0 && rbUsed() == 0) {
    trackEnded = false;
    if (repeatMode == 2) playTrack(cur);
    else {
      int j = nextIndex(1);
      if (j >= 0) playTrack(j);
      else {
        wantPlay = false;
        playTrack(cur);  // listenin sonu: başa sar, dur
        wantPlay = false;
      }
    }
  }

  if (!confirmExit) {
    if (newInfo) {
      newInfo = false;
      if (scr == MS_NOW) {
        drawNowInfo();
        drawProgress();
      } else if (scr == MS_LIB) {
        drawLib(false);
      }
    }
    if (btConnChanged) {
      btConnChanged = false;
      if (scr == MS_NOW) drawHeaderNow(), drawNowInfo();
      else if (scr == MS_BT) drawBt(false);
    }
    if (btListChanged && scr == MS_BT) {
      btListChanged = false;
      drawBt(false);
    }
    static uint32_t lastProg = 0;
    if (scr == MS_NOW && playing && ms - lastProg > 1000) {
      lastProg = ms;
      drawProgress();
    }
  }

  // Durum ara ara saklanır (elektrik kesilirse kaldığı yer kaybolmasın)
  static uint32_t lastSave = 0;
  if (ms - lastSave > 30000) {
    lastSave = ms;
    saveState();
  }
  // Bir dakika dokunulmazsa ekran kısılır (müzik çalmaya devam eder)
  if (!dimmed && ms - lastTouch > 60000) {
    dimmed = true;
    sysSetBacklight(std::max(8, cfg.bright / 8));
  }
  delay(5);
}
