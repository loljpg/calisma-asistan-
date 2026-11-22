#pragma once
// DAXEN VANE sistem katmanı · Tanı: günlüğün SD kopyası, önceki çökmenin özeti, bellek alt
// sınırının günlüğe yazılması, sistem görevlerinin süreç tablosuna eklenmesi, çip bilgisi.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.
#include <esp_core_dump.h>

#define LOG_FILE "/daxen/sys/gunluk.txt"
#define LOG_FILE_OLD "/daxen/sys/gunluk.1.txt"
#define LOG_FILE_MAX (32UL * 1024UL)  // dolunca bir eskisi saklanır, daha eskisi silinir

// Günlük satırını SD'ye ekler (kilit sysLog'da tutuluyor). SD yoksa yalnızca RAM'deki halka kalır.
static void diagSdSink(const char* line) {
  if (!sdOk) return;
  if (!SD.exists("/daxen/sys")) {
    if (!SD.exists("/daxen")) SD.mkdir("/daxen");
    SD.mkdir("/daxen/sys");
  }
  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) return;
  bool full = f.size() > LOG_FILE_MAX;
  f.println(line);
  f.close();
  if (full) {
    if (SD.exists(LOG_FILE_OLD)) SD.remove(LOG_FILE_OLD);
    SD.rename(LOG_FILE, LOG_FILE_OLD);
  }
}

// Panik anında ESP-IDF çekirdek dökümünü flash'taki "coredump" bölümüne yazar. Açılışta özeti
// okunur, günlüğe yazılır ve döküm silinir (bir sonraki açılışta tekrar yazılmasın diye).
static void diagCrashSummary() {
  if (esp_core_dump_image_check() != ESP_OK) return;  // döküm yok ya da bozuk
  esp_core_dump_summary_t s;
  if (esp_core_dump_get_summary(&s) == ESP_OK) {
    char pc[12];
    snprintf(pc, sizeof pc, "0x%08x", (unsigned)s.exc_pc);
    String bt;
    for (uint32_t i = 0; i < s.exc_bt_info.depth && i < 4; i++) {
      char b[12];
      snprintf(b, sizeof b, " 0x%08x", (unsigned)s.exc_bt_info.bt[i]);
      bt += b;
    }
    diagLastCrash = String("görev ") + s.exc_task + ", PC " + pc + ", neden " + String(s.ex_info.exc_cause) +
                    (s.exc_bt_info.corrupted ? ", iz bozuk" : ", iz:" + bt);
    sysLog("çökme", diagLastCrash);
  }
  esp_core_dump_image_erase();
}

// Açılışta: günlüğü SD'ye bağlar, açılış satırını ve varsa önceki çökmeyi yazar
static void diagStart() {
  logSink = diagSdSink;
  sysLog("açılış", "fw-" + String(FW_BUILD) + " " FW_VARIANT ", " + bootReason.substring(bootReason.indexOf(": ") + 2) +
                       (sdOk ? "" : ", SD kart yok"));
  diagCrashSummary();
}

// En düşük boş bellek her 1 KB düştüğünde günlüğe yazılır; o sırada ağ görevinin ne yaptığı da
// eklenir (en düşük değerin tam anı değil, yakın bir ipucu). Saat servisinden çağrılır.
static void diagTick() {
  static uint32_t logged = UINT32_MAX;
  uint32_t m = ESP.getMinFreeHeap();
  if (m + 1024 > logged) return;
  logged = m;
  sysLog("bellek", "en az " + String(m / 1024) + " KB (şimdi boş " + String(ESP.getFreeHeap() / 1024) + " KB, en büyük blok " +
                       String(ESP.getMaxAllocHeap() / 1024) + " KB, ağ: " + netPhase + ")");
}

// Yığını azalan görev: boşluğu 1,5 KB'ın altına inen her görev bir kez günlüğe yazılır (yığın
// boyları bu kayıtlara göre ayarlanır; docs/bellek.md). Saat servisinden, tanıyla birlikte çağrılır.
static void diagStackTick() {
  static int warned[K_MAX] = {0};
  for (int i = 0; i < K_MAX; i++) {
    const Proc& p = kProcs[i];
    if (!p.used || p.kind != PK_TASK || p.done || !p.task || warned[i] == p.pid) continue;
    uint32_t fr = kStackFree(p);
    // eşik: 1,5 KB ya da küçük yığınlarda dörtte biri (tcp/ip'nin 2,5 KB'ı olağan kullanımda 1,4 KB boş)
    if (fr >= (p.stack ? std::min<uint32_t>(1536, p.stack / 4) : 1024u)) continue;
    warned[i] = p.pid;
    sysLog("bellek", "yığın azaldı: " + p.name + " " + String(fr) + " B boş (" + String(p.stack / 1024) + " KB) · açık: " +
                         String(shScreen == SH_APP && curApp ? curApp->name : "ana ekran"));
  }
}

// ESP-IDF ve NimBLE'nin kendi görevleri: adlarıyla bulunur, Görevler'de durum ve yığın boşluğu
// görünür. İşlemci payları ölçülemez (FreeRTOS çalışma süresi sayacı bu derlemede kapalı).
static void diagAdoptSystemTasks() {
  // Yığın boyu biliniyorsa (ESP-IDF ve Arduino ayarlarından) Görevler doluluğu da gösterir; yığın
  // boşluğu 1,5 KB'ın altına inerse günlüğe yazılır (diagStackTick): boylar bu kayıtlarla ayarlanır.
  static const struct {
    const char* task;
    const char* name;
    uint16_t stack;
    const char* note;
  } known[] = {
      {"loopTask", "çekirdek", CONFIG_ARDUINO_LOOP_STACK_SIZE, "arayüz ve servisler (Arduino döngüsü)"},
      {"esp_timer", "zamanlayıcı", CONFIG_ESP_TIMER_TASK_STACK_SIZE, "ESP-IDF zamanlayıcı geri çağrıları"},
      {"wifi", "wifi", 0, "Wi-Fi sürücüsü"},
      {"tiT", "tcp/ip", CONFIG_LWIP_TCPIP_TASK_STACK_SIZE, "TCP/IP yığını (lwIP)"},
      {"nimble_host", "bt-host", 0, "Bluetooth yığını (NimBLE)"},
      {"btController", "bt-denetleyici", 0, "Bluetooth denetleyicisi"},
  };
  for (auto& k : known) {
    TaskHandle_t h = xTaskGetHandle(k.task);
    if (!h) continue;
    BaseType_t core = xTaskGetAffinity(h);
    kAdopt(k.name, h, (uint8_t)uxTaskPriorityGet(h), core == tskNO_AFFINITY ? 0 : (uint8_t)core, k.stack, k.note);
  }
}

static String sysChipText() {
  return "ESP32 rev" + String(ESP.getChipRevision()) + " · " + String(ESP.getCpuFreqMHz()) + " MHz · " +
         String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB";
}
