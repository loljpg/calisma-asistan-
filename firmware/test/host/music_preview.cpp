// Müzik yazılımının arayüz önizlemesi
#include <Arduino.h>
extern uint32_t g_ms;
#define xSemaphoreCreateMutex() ((void*)1)
#define xSemaphoreTake(a, b) (1)
#define xSemaphoreGive(a) (1)
inline void xTaskCreatePinnedToCore(void (*)(void*), const char*, int, void*, int, void*, int) {}
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}
#define setup music_setup
#define loop music_loop
#include "music/music_main.cpp"
#undef setup
#undef loop
extern std::string g_sdroot, g_outdir;
static void shot(const char* name) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s.ppm", g_outdir.c_str(), name);
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6 320 240 255\n");
  uint16_t* b = tft.buf();
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t c = b[i];
    uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}
int main() {
  music_setup();
  shot("m0-muzik-bos");
  playTrack(0);

  durMs = 245000;
  baseMs = 92000;
  wantPlay = true;
  playing = true;
  scr = MS_NOW;
  redraw(true);
  shot("m1-muzik-caliyor");
  go(MS_BT);
  shot("m5-cikis-hoparlor");
  outMode = OUT_HP;
  btTarget = "Sony WH-1000XM4";
  go(MS_LIB);
  shot("m2-muzik-kitaplik");
  btFound = {{"Sony WH-1000XM4", -52}, {"JBL Tune 510BT", -70}, {"Galaxy Buds2", -81}};
  go(MS_BT);
  shot("m3-muzik-kulaklik");
  go(MS_NOW);
  confirmExit = true;
  drawExitDialog();
  shot("m4-muzik-cikis");
  return 0;
}
