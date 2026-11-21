# Çalışma Asistanı

YKS'ye hazırlık için kişisel çalışma asistanı. Telefon uygulaması ödevleri, deneme ve quiz sonuçlarını tutar, Claude ile günlük çalışma planı hazırlar. ESP32 masa saati (CYD) aynı veriyle çalışır: saat, hava durumu, günün planı ve kronometre.

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

## Bulut ve masa saati

Ayarlar → Bulut ve masa saati bölümünden Firebase hesabına bağlanınca:

- Bugünün planı ve özeti masa saatine gönderilir,
- Masa saatinde atılan tikler ve kronometre süreleri uygulamaya işlenir,
- Tüm veriler buluta yedeklenir; yeni telefonda "Buluttan geri yükle" ile geri gelir.

Firebase kurulumu ve veri yapısı: [docs/firebase.md](docs/firebase.md)

Masa saati **DAXEN VANE** adlı küçük bir işletim sistemi çalıştırır: YKS planı ve sayaç, notlar, hava, saat ve alarm, oyunlar, telefondaki müziğin kumandası ve kendi yazdığın Lua uygulamaları. Yazılım yükleme, ilk kurulum ve uygulamalar: [firmware/README.md](firmware/README.md)

## Nasıl derleniyor

`main` dalına `app/` altında bir değişiklik gönderildiğinde GitHub Actions:

1. Mantık testlerini çalıştırır (`app/tests`),
2. Capacitor ile Android projesini oluşturur,
3. APK'yı derleyip `vN` etiketiyle Releases'e koyar.

Uygulama HTML, CSS ve JavaScript ile yazıldı (`app/www`), derleme adımı yok.

Tasarım ("Sentez"): monokrom düzen, silik kareli defter dokusu, tek vurgu rengi olarak kırmızı kalem. Açık ve koyu tema var (Ayarlar → Görünüm; *Otomatik* telefonun ayarını izler). Yazı tipleri uygulamanın içinde: arayüz Inter, başlıklar ve büyük rakamlar Lora (ikisi de SIL Open Font License 1.1).

| Klasör | İçerik |
| --- | --- |
| `app/www/js/logic.js` | Tarih, net, istatistik, plan doğrulama gibi saf mantık |
| `app/www/js/store.js` | Telefonda veri saklama ve yedek |
| `app/www/js/ai.js` | Claude API bağlantısı |
| `app/www/js/cloud.js`, `sync.js` | Firebase girişi ve eşitleme |
| `app/www/js/views/` | Bugün, Program, Denemeler, İstatistik, Ayarlar ekranları |
| `app/www/css/app.css`, `app/www/fonts/` | Tasarım ve gömülü yazı tipleri |
| `app/resources/` | Uygulama ikonu ve onu üreten betik |

## İmza anahtarı hakkında

`app/keys/debug.keystore`, APK'ların her derlemede aynı anahtarla imzalanması için var. Böylece güncellemeler eski sürümün üstüne kurulabiliyor. Bu bir hata ayıklama (debug) anahtarıdır ve sadece kişisel kullanım içindir. Uygulama bir gün Play Store'a çıkacaksa ayrı ve gizli bir yayın anahtarı kullanılmalı. Repo ayarlarında `ANDROID_DEBUG_KEYSTORE_B64` adlı bir secret tanımlanırsa derleme onu tercih eder.
