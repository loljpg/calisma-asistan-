# Çalışma Asistanı

YKS'ye hazırlık için kişisel çalışma asistanı. Telefon uygulaması ödevleri, deneme ve quiz sonuçlarını tutar, Claude ile günlük çalışma planı hazırlar. Sonraki sürümde ESP32 masa saati (CYD) aynı veriyle çalışacak.

<img src="app/resources/icon-preview.png" width="96" alt="Uygulama ikonu">

## Telefona kurulum

1. Bu repodaki **Releases** bölümünden en son `calisma-asistani-vN.apk` dosyasını indir.
2. Dosyayı aç. Android "bilinmeyen kaynaklardan yükleme" izni isterse ver.
3. Play Protect uyarı gösterirse **Yine de yükle**'ye dokun.

Yeni sürüm çıktığında uygulama Bugün ekranında haber verir. Yeni APK eskisinin üstüne kurulur, veriler silinmez.

## Claude ayarı

Uygulamada iki plan modu var (Ayarlar → Claude):

- **Otomatik:** console.anthropic.com adresinden alınan API anahtarıyla tek dokunuşta plan hazırlar. API, Claude Pro aboneliğinden ayrı faturalandırılır.
- **Manuel:** Uygulama hazır bir istek metni oluşturur. Bunu Claude uygulamasına yapıştırıp gelen cevabı geri yapıştırırsın. Ek ücret yoktur.

API anahtarı sadece telefonda saklanır ve yedeğe yazılmaz.

## Nasıl derleniyor

`main` dalına `app/` altında bir değişiklik gönderildiğinde GitHub Actions:

1. Mantık testlerini çalıştırır (`app/tests`),
2. Capacitor ile Android projesini oluşturur,
3. APK'yı derleyip `vN` etiketiyle Releases'e koyar.

Uygulama HTML, CSS ve JavaScript ile yazıldı (`app/www`), derleme adımı yok.

| Klasör | İçerik |
| --- | --- |
| `app/www/js/logic.js` | Tarih, net, istatistik, plan doğrulama gibi saf mantık |
| `app/www/js/store.js` | Telefonda veri saklama ve yedek |
| `app/www/js/ai.js` | Claude API bağlantısı |
| `app/www/js/views/` | Bugün, Program, Denemeler, İstatistik, Ayarlar ekranları |
| `app/resources/` | Uygulama ikonu ve onu üreten betik |

## İmza anahtarı hakkında

`app/keys/debug.keystore`, APK'ların her derlemede aynı anahtarla imzalanması için var. Böylece güncellemeler eski sürümün üstüne kurulabiliyor. Bu bir hata ayıklama (debug) anahtarıdır ve sadece kişisel kullanım içindir. Uygulama bir gün Play Store'a çıkacaksa ayrı ve gizli bir yayın anahtarı kullanılmalı. Repo ayarlarında `ANDROID_DEBUG_KEYSTORE_B64` adlı bir secret tanımlanırsa derleme onu tercih eder.
