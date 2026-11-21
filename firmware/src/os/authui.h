#pragma once
// PIN sorma, yeni PIN ve ekran kilidi (saklama core/auth.h'de). Ekran klavyesi şifre kipinde açılır.
// Ekran kilidi (Ayarlar → Güvenlik): açılışta ve 5 dk dokunulmayınca ana ekran kilitlenir; saat
// görünür, her dokunuş önce PIN ister. Çalan alarm kilitliyken de kapatılabilir. Kilit yalnızca
// ekranı korur: USB seri port ve telefonun Bluetooth bağlantısı çalışmaya devam eder.
// PIN klavyesi: şifre kipinde, rakam katmanıyla açılır
static void pinOpen(const String& title, std::function<void(bool, const String&)> done, const String& hint = "PIN") {
  kbOpen(title, "", done, true, 8, hint);
  kbm.kb.layer = 1;
  kbmDraw();
}

static void authWaitToast() { toast("Çok fazla yanlış PIN: " + String(authWaitSec()) + " sn bekle"); }

static void authAsk(const String& title, std::function<void()> then) {
  if (!authHasPin()) {
    then();
    return;
  }
  if (authWaitSec()) {
    authWaitToast();
    return;
  }
  pinOpen(title, [then](bool ok, const String& v) {
    shellRedraw(true);
    if (!ok) return;
    int r = authCheck(v);
    if (r == 1) then();
    else if (r < 0) authWaitToast();
    else toast("PIN yanlış");
  });
}

static void authNewPin(std::function<void()> done) {
  pinOpen("Yeni PIN (4–8 rakam)", [done](bool ok, const String& a) {
    if (!ok || !authPinValid(a)) {
      shellRedraw(true);
      if (ok) toast("PIN 4–8 rakam olmalı");
      return;
    }
    pinOpen("Yeni PIN'i tekrar gir", [a, done](bool ok2, const String& b) {
      shellRedraw(true);
      if (!ok2) return;
      if (a != b) {
        toast("PIN'ler aynı değil");
        return;
      }
      authSetPin(a);
      toast("PIN belirlendi");
      if (done) done();
    });
  });
}

static void lockNow() {
  if (!cfg.lockOn || !authHasPin() || lockActive) return;
  lockActive = true;
  authDevOff();
  if (kbm.active) kbmFinish(false);
  ov = Overlay();
  homeShow();
  sysLog("güvenlik", "ekran kilitlendi");
}

static void lockAsk() {
  if (authWaitSec()) {
    authWaitToast();
    return;
  }
  pinOpen("Kilidi aç", [](bool ok, const String& v) {
    int r = ok ? authCheck(v) : 0;
    if (r == 1) lockActive = false;
    shellRedraw(true);
    if (ok && r != 1) {
      if (r < 0) authWaitToast();
      else toast("PIN yanlış");
    }
  });
}
