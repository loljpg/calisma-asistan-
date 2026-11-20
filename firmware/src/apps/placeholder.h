#pragma once
// Geçici: henüz yazılmamış uygulamalar için boş sayfa (geliştirme sırasında).
#include "../os/shell.h"
static void phDraw(bool full) {
  if (full) header(curApp ? curApp->name : "");
  emptyState(HDR_H, SCR_H - HDR_H, curApp ? curApp->icon : I_INFO, "Hazırlanıyor", "");
}
#define PLACEHOLDER(VAR, NAME, LABEL, ICON) \
  static const App VAR = {NAME, LABEL, ICON, nullptr, phDraw, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
