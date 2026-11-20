#pragma once
// Uygulama listesi (AppId sırasıyla aynı) ve dosya açma yönlendirmesi.
#include "home.h"

static const App* const APP_TABLE[AID_COUNT] = {
    &APP_YKS,   &APP_NOTES,  &APP_FILES,  &APP_SETTINGS, &APP_MUSIC, &APP_MEDIA, &APP_NOTIFS, &APP_CALC,     &APP_CLOCK, &APP_WEATHER,
    &APP_TODO,  &APP_GAMES,  &APP_PHOTOS, &APP_READER,   &APP_DRAW,  &APP_CARDS, &APP_LIGHT,  &APP_TRANSFER, &APP_SYSINFO,
};
static const App* const* appTable() { return APP_TABLE; }
static int appTableCount() { return AID_COUNT; }
