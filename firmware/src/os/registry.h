#pragma once
// Uygulama listesi (AppId sırasıyla aynı) ve dosya açma yönlendirmesi.
#include "home.h"

static const App* const APP_TABLE[AID_COUNT] = {
    &APP_YKS,   &APP_NOTES,  &APP_FILES,  &APP_SETTINGS, &APP_MEDIA, &APP_NOTIFS, &APP_CALC,     &APP_CLOCK, &APP_WEATHER,
    &APP_TODO,  &APP_GAMES,  &APP_PHOTOS, &APP_READER,   &APP_DRAW,  &APP_CARDS, &APP_LIGHT,  &APP_TRANSFER, &APP_SYSINFO,
    &APP_TASKS,
};
static const App* const* appTable() { return APP_TABLE; }
static int appTableCount() { return AID_COUNT; }

// Dosyayı türüne göre uygun uygulamada açar (Dosyalar'dan, Notlar'dan)
static void openFileWith(const String& path) {
  String e = extOf(path);
  if (e == "jpg" || e == "jpeg" || e == "bmp" || e == "cizim") {
    phOpenPath = path;
    appOpenChild(&APP_PHOTOS);
  } else if (e == "txt" || e == "md" || e == "csv" || e == "log" || e == "json" || e == "ini" || e == "lua") {
    if (path.startsWith(NOTES_DIR "/") && e == "txt") {
      appOpenChild(&APP_NOTES);
      edOpen(path);
    } else {
      rdOpenPath = path;
      appOpenChild(&APP_READER);
    }
  } else if (e == "el") {
    appOpenChild(&APP_NOTES);
    hwOpen(path);
  } else if (e == "bin") {
    alertBox("Yazılım dosyası", "firmware-" FW_VARIANT ".bin adıyla SD kartın köküne koyarsan açılışta yüklenir.");
  } else {
    toast("Bu dosya türü açılamıyor");
  }
}

static void openInReader(const String& path) {
  rdOpenPath = path;
  appOpenChild(&APP_READER);
}
