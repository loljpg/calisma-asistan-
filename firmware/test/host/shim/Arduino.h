#pragma once
// Ev bilgisayarında arayüz önizlemesi için Arduino yerine geçen küçük katman.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include "pgmspace.h"
#include "esp32-hal-log.h"
#include "esp_system.h"
#include "WString.h"
// FreeRTOS yerine
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(x) (x)
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return (void*)1; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, uint32_t) { return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
inline void vTaskDelay(uint32_t) {}
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
inline uint32_t ulTaskNotifyTake(int, uint32_t) { return 0; }
inline void xTaskNotifyGive(TaskHandle_t) {}
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
typedef uint8_t byte;
typedef bool boolean;
#define PI 3.1415926535897932384626433832795
#define HIGH 1
#define LOW 0
#define INPUT 1
#define OUTPUT 3
#define INPUT_PULLUP 5
#define RTC_NOINIT_ATTR
#define IRAM_ATTR
using std::min;
using std::max;
using std::abs;
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
long random(long howbig);
long random(long howsmall, long howbig);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
void pinMode(uint8_t pin, uint8_t mode);
uint16_t analogRead(uint8_t pin);
void ledcWrite(uint8_t ch, uint32_t duty);
float temperatureRead();
struct HostSerial {
  void begin(int) {}
  template <class T> void print(const T&) {}
  template <class T> void println(const T&) {}
  void println() {}
  void printf(const char* f, ...);
};
extern HostSerial Serial;
struct HostEsp {
  uint32_t getFreeHeap() { return 92 * 1024; }
  uint32_t getMinFreeHeap() { return 61 * 1024; }
  uint32_t getMaxAllocHeap() { return 52 * 1024; }
  void restart() { printf("[ESP.restart]\n"); }
  uint32_t getCpuFreqMHz() { return 240; }
  uint32_t getFlashChipSize() { return 4 * 1024 * 1024; }
  uint32_t getSketchSize() { return 1700000; }
};
extern HostEsp ESP;
