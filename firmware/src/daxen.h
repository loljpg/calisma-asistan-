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
#include "apps/placeholder.h"
PLACEHOLDER(APP_NOTES, "Notlar", "Notlar", I_NOTES)
PLACEHOLDER(APP_MUSIC, "Müzik", "Müzik", I_HEADPHONES)
PLACEHOLDER(APP_CALC, "Hesap makinesi", "Hesap Mak.", I_CALC)
PLACEHOLDER(APP_CLOCK, "Saat", "Saat", I_ALARM)
PLACEHOLDER(APP_WEATHER, "Hava durumu", "Hava", I_PARTLY)
PLACEHOLDER(APP_TODO, "Yapılacaklar", "Yapılacaklar", I_TODO)
PLACEHOLDER(APP_PHOTOS, "Fotoğraflar", "Fotoğraflar", I_PHOTO)
PLACEHOLDER(APP_READER, "Okuyucu", "Okuyucu", I_BOOK)
PLACEHOLDER(APP_DRAW, "Çizim", "Çizim", I_PENCIL)
PLACEHOLDER(APP_CARDS, "Bilgi kartları", "Kartlar", I_CARDS)
PLACEHOLDER(APP_LIGHT, "Işık ölçer", "Işık ölçer", I_LIGHT)
PLACEHOLDER(APP_TRANSFER, "Dosya aktarımı", "Aktarım", I_TRANSFER)

#include "os/home.h"
#include "os/registry.h"
static void openFileWith(const String& path) { toast(baseName(path)); }
static int luaAppCount() { return 0; }
static String luaAppLabel(int) { return ""; }
static void luaAppOpen(int) {}
