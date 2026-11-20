#pragma once
// Telefondan gelenler: Bildirimler (liste ve ayrıntı) ve Medya (telefonda çalanı yönetme).
#include "../os/shell.h"

// ============================================================================
// Bildirimler
// ============================================================================
static int ntFirst = 0, ntDetail = -1;
static const int NT_ROW = 44, NT_ROWS = 4;

static String agoStr(uint32_t at) {
  if (!at) return "";
  uint32_t m = (millis() - at) / 60000;
  if (m < 1) return "şimdi";
  if (m < 60) return String(m) + " dk";
  return String(m / 60) + " sa";
}

static void ntDrawList() {
  if (notifs.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_BELL, "Bildirim yok",
               bleLinked ? String("Telefona gelen bildirimler burada görünür.") : String("Telefon Bluetooth ile bağlıyken bildirimler burada görünür."));
    return;
  }
  int n = notifs.size();
  ntFirst = constrain(ntFirst, 0, std::max(0, n - NT_ROWS));
  for (int r = 0; r < NT_ROWS + 1; r++) {
    int y = HDR_H + r * NT_ROW, i = ntFirst + r;
    int h = std::min(NT_ROW, SCR_H - y);
    fillBg(0, y, SCR_W, h);
    if (r >= NT_ROWS || i >= n) continue;
    const Notif& nf = notifs[i];
    drawAppIcon(nf.icon, nf.app, 12, y + 12, C_BG);
    String title = nf.title.length() ? nf.title : nf.app;
    String ago = agoStr(nf.at);
    int aw = ago.length() ? textW(ago, F_XS) + 8 : 0;
    text(fit(title, F_MD, 260 - aw), 44, y + 5, F_MD, C_TEXT, C_BG);
    if (aw) text(ago, 308, y + 8, F_XS, C_TEXT3, C_BG, TR_DATUM);
    String sub = nf.app + (nf.text.length() ? " · " + nf.text : String(""));
    text(fit(sub, F_XS, 262), 44, y + 26, F_XS, C_TEXT2, C_BG);
    hline(44, y + NT_ROW - 1, 264);
  }
  scrollBar(HDR_H, NT_ROW * NT_ROWS, ntFirst, NT_ROWS, n);
}

static void ntDrawDetail() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  if (ntDetail < 0 || ntDetail >= (int)notifs.size()) return;
  const Notif& nf = notifs[ntDetail];
  drawAppIcon(nf.icon, nf.app, 12, HDR_H + 10, C_BG);
  text(fit(nf.app, F_SM, 200), 42, HDR_H + 13, F_SM, C_TEXT2, C_BG);
  text(agoStr(nf.at), 308, HDR_H + 13, F_XS, C_TEXT3, C_BG, TR_DATUM);
  int y = HDR_H + 40;
  if (nf.title.length()) {
    auto ts = wrapText(nf.title, F_MD, 296, 2);
    for (auto& l : ts) {
      text(l, 12, y, F_MD, C_TEXT, C_BG);
      y += 22;
    }
    y += 4;
  }
  auto ls = wrapText(nf.text, F_SM, 296, (SCR_H - y - 8) / 19);
  for (auto& l : ls) {
    text(l, 12, y, F_SM, C_TEXT2, C_BG);
    y += 19;
  }
}

static void ntDraw(bool full) {
  if (full) {
    header(ntDetail >= 0 ? String("Bildirim") : String("Bildirimler"));
    if (ntDetail < 0 && !notifs.empty()) headerAction(0, I_TRASH);
  }
  if (ntDetail >= 0) ntDrawDetail();
  else ntDrawList();
}

static void ntTap(int x, int y) {
  if (y < HDR_H) {
    if (ntDetail < 0 && !notifs.empty() && headerHit(x, y) == 0)
      dialog("Bildirimler temizlensin mi?", "Telefondaki bildirimler silinmez.", {"Vazgeç", "Temizle"}, [](int c) {
        if (c == 1) {
          notifs.clear();
          ntFirst = 0;
          shellRedraw(true);
        }
      });
    return;
  }
  if (ntDetail >= 0) return;
  int i = ntFirst + (y - HDR_H) / NT_ROW;
  if ((y - HDR_H) / NT_ROW < NT_ROWS && i < (int)notifs.size()) {
    ntDetail = i;
    ntDraw(true);
  }
}

static void ntSwipe(int dir) {
  if (ntDetail >= 0 || (dir != SW_UP && dir != SW_DOWN)) return;
  int n = notifs.size();
  int nf = constrain(ntFirst + (dir == SW_UP ? NT_ROWS - 1 : -(NT_ROWS - 1)), 0, std::max(0, n - NT_ROWS));
  if (nf != ntFirst) {
    ntFirst = nf;
    ntDrawList();
  }
}

static bool ntBack() {
  if (ntDetail >= 0) {
    ntDetail = -1;
    ntDraw(true);
    return true;
  }
  return false;
}

static void ntOpen() {
  ntDetail = -1;
  ntFirst = 0;
}

static const App APP_NOTIFS = {"Bildirimler", "Bildirimler", I_BELL, ntOpen, ntDraw, ntTap, nullptr, ntSwipe, nullptr, nullptr, ntBack, nullptr};

// ============================================================================
// Medya: telefonda çalan şarkı (kapak, ilerleme, kumanda, ses)
// ============================================================================
static const int MD_CTRL_Y = 158, MD_VOL_Y = 206;

static uint32_t mediaPos() {
  uint32_t p = media.playing ? media.pos + (millis() - media.at) : media.pos;
  return media.dur ? std::min(p, media.dur) : p;
}

static void mdDrawProgress() {
  fillBg(12, 126, 296, 26);
  int pct = media.dur ? (int)((uint64_t)mediaPos() * 100 / media.dur) : 0;
  progress(12, 130, 296, 2, pct, C_ACC);
  text(fmtMs(mediaPos()), 12, 138, F_XS, C_TEXT2, C_BG);
  if (media.dur) text(fmtMs(media.dur), 308, 138, F_XS, C_TEXT2, C_BG, TR_DATUM);
}

static void mdDrawControls() {
  iconButton(52, MD_CTRL_Y, 40, I_PREV);
  panel(124, MD_CTRL_Y - 2, 72, 44, C_ACCBG, C_ACC);
  drawIcon(media.playing ? I_PAUSE : I_PLAY, 148, MD_CTRL_Y + 8, 24, C_TEXT, C_ACCBG);
  iconButton(228, MD_CTRL_Y, 40, I_NEXT);
}

static void mdDrawVolume() {
  fillBg(0, MD_VOL_Y, SCR_W, 28);
  drawIcon(I_VOLUME, 12, MD_VOL_Y + 4, 20, C_TEXT2, C_BG);
  iconButton(40, MD_VOL_Y, 28, I_MINUS);
  int pct = media.volMax > 0 ? media.vol * 100 / media.volMax : 0;
  progress(76, MD_VOL_Y + 13, 192, 2, pct, C_TEXT);
  iconButton(280, MD_VOL_Y, 28, I_PLUS);
}

static void mdDrawCover() {
  const int X = 12, Y = 38;
  if (artPx && artId && artId == media.art) {
    tft.pushImage(X, Y, ART_PX, ART_PX, artPx);
    tft.drawRect(X - 1, Y - 1, ART_PX + 2, ART_PX + 2, C_LINE);
    return;
  }
  panel(X, Y, ART_PX, ART_PX, C_SURF, C_LINE);
  drawIcon(I_NOTE, X + 24, Y + 24, 32, C_TEXT3, C_SURF);
}

static void mdDraw(bool full) {
  if (full) header("Medya");
  if (!bleLinked || !media.on) {
    String l1, l2;
    if (!bleLinked) {
      l1 = "Telefon bağlı değil";
      l2 = "Ayarlar → Bluetooth ile telefonu eşleştir.";
    } else if (!media.enabled) {
      l1 = "Müzik kumandası kapalı";
      l2 = "Telefondaki uygulamanın ayarlarından açabilirsin.";
    } else {
      l1 = "Telefonda müzik çalmıyor";
      l2 = "Spotify ya da YouTube Music'te bir şarkı aç.";
    }
    emptyState(HDR_H, SCR_H - HDR_H, I_REMOTE, l1, l2);
    return;
  }
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  mdDrawCover();
  auto ts = wrapText(media.title.length() ? media.title : String("Bilinmeyen şarkı"), F_MD, 204, 2);
  int y = 40;
  for (auto& l : ts) {
    text(l, 104, y, F_MD, C_TEXT, C_BG);
    y += 22;
  }
  text(fit(media.artist, F_SM, 204), 104, y + 2, F_SM, C_TEXT2, C_BG);
  text(fit(media.app + (media.playing ? "" : " · duraklatıldı"), F_XS, 204), 104, 104, F_XS, C_TEXT3, C_BG);
  mdDrawProgress();
  mdDrawControls();
  mdDrawVolume();
}

static void mdTap(int x, int y) {
  if (!bleLinked || !media.on || y < HDR_H) return;
  if (y >= MD_CTRL_Y - 6 && y < MD_CTRL_Y + 46) {
    if (x < 110) bleMediaKey("prev");
    else if (x < 210) {
      bleMediaKey("toggle");
      media.pos = mediaPos();
      media.at = millis();
      media.playing = !media.playing;
      mdDrawControls();
    } else bleMediaKey("next");
  } else if (y >= MD_VOL_Y - 6) {
    if (x < 76) {
      bleMediaKey("voldown");
      if (media.vol > 0) media.vol--;
      mdDrawVolume();
    } else if (x >= 272) {
      bleMediaKey("volup");
      if (media.vol < media.volMax) media.vol++;
      mdDrawVolume();
    }
  }
}

static void mdTick() {
  static uint32_t last = 0;
  if (media.on && media.playing && bleLinked && millis() - last >= 1000 && !toastUntil) {
    last = millis();
    mdDrawProgress();
  }
}

static const App APP_MEDIA = {"Medya", "Medya", I_REMOTE, nullptr, mdDraw, mdTap, nullptr, nullptr, nullptr, mdTick, nullptr, nullptr};
