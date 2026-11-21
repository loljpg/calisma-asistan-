#pragma once
// DAXEN VANE sistem katmanı · Kullanılmayan kütüphane parçalarının yerine küçük karşılıklar.
// platformio.ini'deki -Wl,--wrap=<ad> bağlayıcıya o işleve yapılan bütün çağrıları __wrap_<ad>'a
// yönlendirtir; asıl işlev artık çağrılmadığı için yazılıma girmez.
//
// sscanf (yaklaşık 10 KB): yazılımda yalnızca Bluetooth denetleyicisinin eş yaşam (coex) sürümünü
// "v%u.%u.%u" diye okuması kullanıyor. Yerine core/miniscan.h (tamsayı biçimleri; testli).
// mbedtls_ssl_handshake_server_step (7 KB): kart hiçbir zaman TLS sunucusu olmaz (web sunucusu
// yerel ağda düz HTTP). Çağrılırsa "özellik yok" hatası döner.
// NimBLEScan::onHostSync: sys/blescan.h'de.
extern "C" int __wrap_sscanf(const char* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = miniVscan(s, fmt, ap);
  va_end(ap);
  return n;
}
extern "C" int __wrap_mbedtls_ssl_handshake_server_step(void*) { return -0x7080; }  // MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE
