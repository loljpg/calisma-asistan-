#pragma once
// Bir kerelik kalıcı ayar (NVS) geçişleri. Geçiş sürümü "cs/mig" anahtarındadır; her adım bir kez çalışır,
// sonraki açılışlarda yalnızca bu anahtar okunur. Kullanıcı ayarlarına dokunulmaz.
//   1  müzik yazılımı kaldırıldı: "mus" (çalma konumu, ses, kulaklık) ve "mus2" (modül özeti) ad alanları
//      ile "cs" içindeki "getmus" ve "mdl" (indirme işaretleri) silinir
#include <Preferences.h>

#define STORE_MIG 1

// Uygulanan geçiş sürümünü döndürür (çağıran kilidi tutar)
static int storeMigrate(Preferences& p) {
  p.begin("cs", true);
  int v = p.getInt("mig", 0);
  p.end();
  if (v >= STORE_MIG) return v;
  if (v < 1) {
    if (p.begin("mus", false)) {
      p.clear();
      p.end();
    }
    if (p.begin("mus2", false)) {
      p.clear();
      p.end();
    }
    p.begin("cs", false);
    p.remove("getmus");
    p.remove("mdl");
    p.end();
  }
  p.begin("cs", false);
  p.putInt("mig", STORE_MIG);
  p.end();
  return STORE_MIG;
}
