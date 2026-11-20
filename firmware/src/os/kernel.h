#pragma once
// DAXEN çekirdeği: donanım zamanlayıcı kesmesi ve süreç zamanlayıcı.
//
// Donanım zamanlayıcısı 3, saniyede 1000 kez kesme üretir (kTicks = çekirdek saati, ms). Sıradaki
// servisin vakti geldiğinde kesme, çekirdek görevini (Arduino döngü görevi, öncelik 3, işlemci
// çekirdeği 1) uyandırır. FreeRTOS o an çalışan düşük öncelikli süreci (Lua uygulaması, satranç
// motoru) olduğu yerde keser ve çekirdeğe geçer. Çekirdek vakti gelen servisleri öncelik
// sırasıyla çalıştırır, sürelerini ölçer ve bir sonraki vakte kadar uyur; bu arada işlemci
// süreçlerindir.
//
// Süreç türleri:
//  - Servis: kısa, periyodik iş (dokunma, kabuk, Bluetooth, web, saat, ışık). Çekirdek
//    görevinde, ekran kilidi tutularak çalışır; ekrana serbestçe çizer.
//  - Görev: kendi yığını olan uzun iş (ağ, Lua uygulaması, satranç). FreeRTOS görevi olarak
//    çalışır ve kesmeyle zaman dilimlenir. Ekrana yalnızca Gfx kilidiyle çizer.
//
// Sonlandırma işbirliklidir: kKill() isteği bırakır, süreç bunu kendisi görüp çıkar (Lua her
// 1000 komutta bir, satranç her düğümde bakar). Böylece yarım kalmış SD yazması ya da tutulan bir
// kilitle ölen süreç olmaz.
#include "../core/state.h"

#define K_HZ 1000
#define K_MAX 14
#define K_CORE_PRIO 3   // çekirdek (arayüz) görevi
#define K_PROC_PRIO 1   // kullanıcı süreçleri

enum ProcKind : uint8_t { PK_SERVICE, PK_TASK };

struct Proc {
  bool used = false;
  int pid = 0;
  String name;
  uint8_t kind = PK_SERVICE;
  uint8_t prio = 0;      // servis: sıra (küçük önce); görev: FreeRTOS önceliği
  uint8_t core = 1;      // işlemci çekirdeği
  uint16_t period = 0;   // ms, servis
  void (*svc)() = nullptr;
  void (*entry)(Proc*) = nullptr;
  uint32_t due = 0;      // çekirdek saati, servis
  TaskHandle_t task = nullptr;
  uint32_t stack = 0;
  volatile bool killReq = false;
  volatile bool done = false;
  bool killable = false;
  bool measured = true;  // CPU süresi ölçülüyor mu (ağ görevi çoğunlukla ağı bekler, ölçülmez)
  uint32_t runs = 0, lastUs = 0, maxUs = 0;
  uint64_t totalUs = 0;
  volatile uint32_t winUs = 0;  // içinde bulunulan 1 saniyelik pencere
  uint16_t cpu10 = 0;           // son pencerede işlemci payı, binde
  uint32_t startedMs = 0;
};

static Proc kProcs[K_MAX];
static int kNextPid = 1, kServiceCount = 0;
static volatile uint32_t kTicks = 0;    // kesme sayacı
static volatile uint32_t kWakeAt = 0;   // çekirdeğin uyandırılacağı tik
static uint32_t kWakes = 0, kLateRuns = 0;
static TaskHandle_t kCoreTask = nullptr;
static SemaphoreHandle_t gfxMtx = nullptr;
static volatile int gfxDepth = 0;
static uint32_t kWinStart = 0;
static uint16_t kLoad10 = 0;  // işlemci çekirdeği 1 yükü, binde
static bool kTimerOn = false;

// --- Ekran kilidi: ekrana iki görevden aynı anda çizilmesin ---
struct Gfx {
  Gfx() {
    if (gfxMtx) xSemaphoreTakeRecursive(gfxMtx, portMAX_DELAY);
    gfxDepth++;
  }
  ~Gfx() {
    gfxDepth--;
    if (gfxMtx) xSemaphoreGiveRecursive(gfxMtx);
  }
};
// Kilidi tutan görev beklerken kilidi tamamen bırakır (başka bir süreci beklerken kilitlenme olmasın)
static int gfxReleaseAll() {
  int d = gfxDepth;
  for (int i = 0; i < d; i++) {
    gfxDepth--;
    if (gfxMtx) xSemaphoreGiveRecursive(gfxMtx);
  }
  return d;
}
static void gfxReacquire(int d) {
  for (int i = 0; i < d; i++) {
    if (gfxMtx) xSemaphoreTakeRecursive(gfxMtx, portMAX_DELAY);
    gfxDepth++;
  }
}

static uint32_t kNow() { return kTimerOn ? (uint32_t)kTicks : millis(); }

// --- Zamanlayıcı kesmesi ---
#ifndef HOST
static hw_timer_t* kTimer = nullptr;
static void IRAM_ATTR kIsr() {
  uint32_t t = ++kTicks;
  if ((int32_t)(t - kWakeAt) >= 0 && kCoreTask) {
    kWakeAt = t + 0x40000000UL;  // çekirdek yeni vakti yazana kadar bir kez
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(kCoreTask, &hp);
    if (hp == pdTRUE) portYIELD_FROM_ISR();
  }
}
#endif

// --- Süreç tablosu ---
static Proc* kFind(int pid) {
  if (pid <= 0) return nullptr;
  for (auto& p : kProcs)
    if (p.used && p.pid == pid) return &p;
  return nullptr;
}

static Proc* kAlloc(const char* name) {
  for (auto& p : kProcs)
    if (!p.used) {
      p = Proc();
      p.used = true;
      p.pid = kNextPid++;
      p.name = name;
      p.startedMs = millis();
      return &p;
    }
  return nullptr;
}

static void kAddBusy(Proc* p, uint32_t us) {
  if (!p) return;
  p->winUs += us;
  p->totalUs += us;
  p->runs++;
  p->lastUs = us;
  if (us > p->maxUs) p->maxUs = us;
}

// Periyodik servis ekler. Kayıt sırası önceliktir: önce eklenen önce çalışır.
static int kService(const char* name, uint16_t periodMs, void (*fn)()) {
  Proc* p = kAlloc(name);
  if (!p) return -1;
  p->kind = PK_SERVICE;
  p->period = periodMs ? periodMs : 1;
  p->svc = fn;
  p->prio = kServiceCount++;
  p->core = 1;
  p->due = kNow() + p->prio;  // aynı tike yığılmasınlar
  return p->pid;
}

#ifndef HOST
static void kTrampoline(void* a) {
  Proc* p = (Proc*)a;
  p->entry(p);
  p->task = nullptr;
  p->done = true;  // bundan sonra p'ye dokunulmaz: yuva yeniden kullanılabilir
  vTaskDelete(nullptr);
}
#endif

// Kendi yığınıyla çalışan bir süreç başlatır. Önizlemede (HOST) eşzamanlı çalışır.
static int kSpawn(const char* name, void (*entry)(Proc*), uint32_t stack, bool killable = true, uint8_t prio = K_PROC_PRIO,
                  uint8_t core = 1) {
  Proc* p = kAlloc(name);
  if (!p) return -1;
  p->kind = PK_TASK;
  p->entry = entry;
  p->stack = stack;
  p->prio = prio;
  p->core = core;
  p->killable = killable;
#ifndef HOST
  if (xTaskCreatePinnedToCore(kTrampoline, name, stack, p, prio, &p->task, core) != pdPASS) {
    p->used = false;
    return -1;
  }
#else
  entry(p);
  p->done = true;
#endif
  return p->pid;
}

// Var olan bir FreeRTOS görevini tabloya ekler (ağ görevi)
static int kAdopt(const char* name, TaskHandle_t h, uint8_t prio, uint8_t core, uint32_t stack) {
  Proc* p = kAlloc(name);
  if (!p) return -1;
  p->kind = PK_TASK;
  p->task = h;
  p->prio = prio;
  p->core = core;
  p->stack = stack;
  p->measured = false;
  return p->pid;
}

static bool kAlive(int pid) {
  Proc* p = kFind(pid);
  return p && !p->done;
}

// Sistemin kendi sürecini durdurur (sonlandırılamaz işaretli olsa da)
static void kStop(int pid) {
  Proc* p = kFind(pid);
  if (p && p->kind == PK_TASK) p->killReq = true;
}

static void kKill(int pid) {
  Proc* p = kFind(pid);
  if (p && p->kind == PK_TASK && p->killable) p->killReq = true;
}

// Süreç bitene kadar bekler (en çok ms). Beklerken ekran kilidi bırakılır.
static bool kWait(int pid, uint32_t ms) {
  if (!kAlive(pid)) return true;
  int d = gfxReleaseAll();
  uint32_t t0 = millis();
  bool ok = false;
  while (millis() - t0 < ms) {
    if (!kAlive(pid)) {
      ok = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(4));
#ifdef HOST
    break;
#endif
  }
  gfxReacquire(d);
  return ok || !kAlive(pid);
}

static void kNotify(int pid) {
#ifndef HOST
  Proc* p = kFind(pid);
  if (p && p->task) xTaskNotifyGive(p->task);
#endif
}

static const char* kStateText(const Proc& p) {
  if (p.kind == PK_SERVICE) return "servis";
  if (p.done) return "bitti";
  if (p.killReq) return "sonlanıyor";
#ifndef HOST
  if (p.task) switch (eTaskGetState(p.task)) {
      case eRunning: return "çalışıyor";
      case eReady: return "hazır";
      case eBlocked: return "bekliyor";
      case eSuspended: return "askıda";
      default: break;
    }
#endif
  return "bekliyor";
}

static uint32_t kStackFree(const Proc& p) {
#ifndef HOST
  if (p.kind == PK_TASK && p.task && !p.done) return uxTaskGetStackHighWaterMark(p.task);
#endif
  return 0;
}

// 1 saniyelik pencere: işlemci payları, biten süreçlerin yuvaları
static void kWindow() {
  uint32_t now = millis();
  uint32_t span = now - kWinStart;
  if (span < 1000) return;
  kWinStart = now;
  uint32_t load = 0;
  for (auto& p : kProcs) {
    if (!p.used) continue;
    if (p.kind == PK_TASK && p.done) {
      p.used = false;
      continue;
    }
    uint32_t w = p.winUs;
    p.winUs = 0;
    p.cpu10 = (uint16_t)std::min<uint32_t>(1000, (uint64_t)w / span);  // µs / ms = binde
    if (p.core == 1 && p.measured) load += p.cpu10;
  }
  kLoad10 = std::min<uint32_t>(load, 1000);
}

// Çekirdek döngüsü: kesmeyi bekle, vakti gelen servisleri çalıştır
static void kernelLoop() {
#ifndef HOST
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));  // 25 ms: kesme gelmezse yedek
#endif
  kWakes++;
  {
    Gfx g;
    for (auto& p : kProcs) {
      if (!p.used || p.kind != PK_SERVICE) continue;
      uint32_t now = kNow();
      if ((int32_t)(now - p.due) < 0) continue;
      if ((int32_t)(now - p.due) > (int32_t)p.period) kLateRuns++;
      uint32_t t0 = micros();
      p.svc();
      kAddBusy(&p, micros() - t0);
      p.due += p.period;
      if ((int32_t)(kNow() - p.due) > 0) p.due = kNow() + p.period;  // geride kaldıysa kaçırılanlar atlanır
    }
  }
  kWindow();
  uint32_t next = kNow() + 1000;
  for (auto& p : kProcs)
    if (p.used && p.kind == PK_SERVICE && (int32_t)(p.due - next) < 0) next = p.due;
  kWakeAt = next;
}

static void kernelBegin() {
  if (!gfxMtx) gfxMtx = xSemaphoreCreateRecursiveMutex();
#ifndef HOST
  kCoreTask = xTaskGetCurrentTaskHandle();
  vTaskPrioritySet(nullptr, K_CORE_PRIO);
  kTicks = millis();
  kTimer = timerBegin(3, 80, true);  // 80 MHz / 80 = 1 MHz
  if (kTimer) {
    timerAttachInterrupt(kTimer, &kIsr, false);
    timerAlarmWrite(kTimer, 1000000 / K_HZ, true);
    timerAlarmEnable(kTimer);
    kTimerOn = true;
  }
#endif
  kWinStart = millis();
}
