#pragma once
// DAXEN VANE arayüzü: tasarım sistemi, kabuk ve uygulamalar (sırası önemli).
// main.cpp (cihaz) ve ev önizleme düzeneği aynı dosyayı içerir.
#include "core/types.h"
#include "core/state.h"
#include "core/util.h"
#include "core/sysapi.h"
#include "ui/theme.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "ui/keyboard.h"
#include "os/shell.h"

#include "apps/yks.h"
#include "apps/settings.h"
#include "apps/phone.h"
#include "apps/files.h"
#include "apps/games.h"
#include "apps/transfer.h"
#include "apps/todo.h"
#include "apps/calc.h"
#include "apps/clock.h"
#include "apps/weather.h"
#include "apps/notes.h"
#include "apps/music.h"
#include "apps/placeholder.h"
PLACEHOLDER(APP_PHOTOS, "Fotoğraflar", "Fotoğraflar", I_PHOTO)
PLACEHOLDER(APP_READER, "Okuyucu", "Okuyucu", I_BOOK)
PLACEHOLDER(APP_DRAW, "Çizim", "Çizim", I_PENCIL)
PLACEHOLDER(APP_CARDS, "Bilgi kartları", "Kartlar", I_CARDS)
PLACEHOLDER(APP_LIGHT, "Işık ölçer", "Işık ölçer", I_LIGHT)

#include "os/home.h"
#include "os/registry.h"
static void openFileWith(const String& path) { toast(baseName(path)); }
static int luaAppCount() { return 0; }
static String luaAppLabel(int) { return ""; }
static void luaAppOpen(int) {}
