#pragma once
#include <Arduino.h>
typedef uint8_t esp_bd_addr_t[6];
typedef int esp_a2d_connection_state_t;
struct Frame { int16_t channel1, channel2; };
class BluetoothA2DPSource {
 public:
  bool conn = true;
  void set_local_name(const char*) {}
  void set_ssid_callback(bool (*)(const char*, esp_bd_addr_t, int)) {}
  void set_auto_reconnect(bool) {}
  void set_on_connection_state_changed(void (*)(esp_a2d_connection_state_t, void*), void* = nullptr) {}
  void set_avrc_passthru_command_callback(void (*)(uint8_t, bool)) {}
  void start(int32_t (*)(Frame*, int32_t)) {}
  void end() {}
  bool is_connected() { return conn; }
  void disconnect() {}
  void clean_last_connection() {}
};
