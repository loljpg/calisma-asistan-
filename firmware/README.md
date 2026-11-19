# Masa saati yazılımı (ESP32 CYD)

ESP32-2432S028R ("Cheap Yellow Display") için. Ekstra parça gerekmez: kart, USB kablosu ve
powerbank yeterli.

## Ekranlar

<img src="onizleme.png" width="720" alt="Ekranların yaklaşık önizlemesi: üstte gündüz, altta gece">

| Ekran | Ne gösterir |
| --- | --- |
| **Ana** | Saat, tarih (6 sn'de bir YKS'ye kalan gün ve seriyle değişir), Sakarya hava durumu, şimdiki/sıradaki oturum, günlük hedef çubuğu |
| **Program** | Günün planı. Sağdaki kareye dokun: tamamlandı / geri al. Satıra dokun: o dersle kronometre. Satıra **basılı tut**: menü (kronometreyle başlat, ertele, işareti kaldır) |
| **Kronometre** | Başlat / Duraklat, Bitir (telefona oturum olarak gider), Sıfırla, Pomodoro (25 dk'da mola uyarısı). Telefondaki kronometreyle ortak: birinde başlatınca diğerinde de çalışır |
| **Müzik** | Telefonda çalan şarkı: kapak, ad, sanatçı, ilerleme. Önceki / oynat-durdur / sonraki, ses − + |
| **Oyun** | Blok Patlat: yandaki parçayı sürükleyip 8×8 tahtaya bırak, dolan satır ve sütunlar patlar. Skor ve en iyi skor saklanır |
| **Ayarlar** (dişli) | 1. sayfa: Wi-Fi ve hesap, Bluetooth eşleştirme, tema, kapak rengini temaya uygula. *Diğer ayarlar*: dokunmatik ayarı, renkleri ters çevir, güncelleme, Bluetooth eşleşmesini sil |

Alt çubuktaki simgeler soldan sağa: Ana, Program, Kronometre, Müzik, Oyun, Ayarlar.

**Kedi:** ana ekranda saatin altında topuyla oynayan küçük bir kedi var. Kendi kendine dolaşır,
oturur, topu kovalar; gece 22:00–07:00 uyur. Üstüne dokununca sevinir. Kareleri
`tools/make_cat.py` ile üretilir. **Seri:** tarihin yanında alevle gösterilir.

**Kart kendiliğinden yeniden başlarsa:** Ayarlar → *Diğer ayarlar* sayfasının altında son açılışın
sebebi (çökme, voltaj düştü, güç verildi…), önceki çalışma süresi ve en düşük boş bellek yazar.
Kronometre kaldığı yerden sürer, gönderilmiş tikler ekranda kalır.

**Bildirimler:** telefona gelen bildirim birkaç saniye ekranın üstünde görünür, sonra ana ekranda
tarihin yanında küçük uygulama simgesi olarak kalır. Simgelere dokununca liste açılır (*Temizle*).

**Bluetooth:** uygulamada Ayarlar → *Masa saatini bul*. İlk eşleşmede masa saatinde bir şey
yapman gerekmez; başka bir telefonla eşleştirmek için önce masa saatinde Ayarlar → *Bluetooth*'a dokun
(2 dakika eşleşme kipi). Bluetooth bağlıyken Wi-Fi gerekmez: plan, tikler, kronometre ve saat telefondan gelir.
Ayrıntılar: [docs/bluetooth.md](../docs/bluetooth.md).

**BOOT tuşu:** kısa bas → sonraki ekran, 2 saniye bas → Ayarlar, 6 saniye bas → kurulum portalı
(Wi-Fi ve hesap). Açılışta "Dokunmatik ayarı için BOOT'a bas" yazarken basarsan dokunmatik
yeniden ayarlanır.

**Işık:** yeşil = kronometre çalışıyor, mavi yanıp sönme = 2 dk içinde oturum başlıyor ya da
Pomodoro molası, kırmızı = buluta bağlanamıyor. Ekran 22:00–07:00 arası kısılır; ilk dokunuş
yalnızca ekranı açar.

**Tema:** telefondaki uygulamayla aynı "Sentez" tasarımı. *Otomatik* (varsayılan) gündüz açık
kâğıt, 22:00–07:00 arası koyu görünüm kullanır. Ayarlar → *Tema* satırına her dokunuş
Otomatik / Açık / Koyu arasında geçer.

## İlk yükleme (telefondan, OTG ile)

Önce kartın hangi model olduğuna bak:

| Kart | Yüklenecek dosya |
| --- | --- |
| Tek USB (sadece micro-USB) | `ilk-kurulum-cyd.bin` |
| İki USB (micro-USB **ve** USB-C) | `ilk-kurulum-cyd_st7789.bin` |

1. [Releases](https://github.com/loljpg/calisma-asistan-/releases) sayfasından en yeni
   **fw-v…** sürümündeki doğru dosyayı indir.
2. Play Store'dan **ESPFlash** uygulamasını kur. Kartı telefona bağla, USB iznini ver.
3. Dosyayı seç, adres **0x0**, hız 460800 (olmazsa 115200). Yaz.
4. Bağlanamazsa: kartta **BOOT** tuşunu basılı tut, **RST**'ye bir kez bas, BOOT'u bırak ve
   tekrar dene.

**Kablo:** veri taşıyan bir kablo olmalı (yalnız şarj eden kablolar çalışmaz). İki USB'li kartın
USB-C girişinde gerekli dirençler yok, bu yüzden **iki ucu USB-C olan kablo çalışmaz**: kart
ne yazılım alır ne de açılır. Telefondan USB-C → micro-USB kablo kullan ya da telefona USB-C → USB-A
OTG çevirici takıp normal kablo bağla. Powerbank için de aynısı geçerli: powerbank'in USB-A
çıkışını kullan.

Ekran beyaz kalır ya da karışık görünürse diğer dosyayı dene. Sadece renkler ters ise
Ayarlar → *Renkleri ters çevir* yeterli. Kırmızı yerine mavi görüyorsan bunu bildir
(renk sırası ayarı gerekir).

## İlk açılış

1. **Dokunmatik ayarı:** ekrandaki üç artının ortasına sırayla dokun (BOOT ile atlanabilir).
2. **Kurulum portalı:** telefonun Wi-Fi listesinde `CalismaSaati-Kurulum` ağına bağlan. Açılan
   sayfada *Configure WiFi* → ev ağını seç, şifresini yaz. Altta Firebase alanlarını doldur:
   - Veritabanı adresi: `https://…firebasedatabase.app`
   - Web API anahtarı (uygulamadaki ile aynı)
   - Uygulamada kullandığın e-posta ve şifre
3. *Save*'e bas. Kart ağa bağlanır, saat ve hava durumu gelir, plan telefondan çekilir.

Sayfa kendiliğinden açılmazsa tarayıcıda `192.168.4.1` adresine git. Bilgileri sonradan
Ayarlar → *Wi-Fi ve hesap ayarları* ile değiştirebilirsin (şifre alanı boş kalırsa eski şifre
korunur).

## Güncellemeler

Kart her açılışta, Bluetooth başlamadan önce GitHub'daki en yeni **fw-v…** sürümünü denetler,
varsa kendisi indirip kurar (Bluetooth açıkken şifreli bağlantıya bellek yetmiyor). Her gece
04:00–04:30 arasında, kimse kullanmıyorsa kendini yeniden başlatır; böylece günde bir denetlenir.
Ayarlar → *Diğer ayarlar* → *Güncellemeyi denetle* kartı yeniden başlatıp hemen denetler.

**SD karttan (fw-5 ve sonrası):** Releases'taki `firmware-cyd_st7789.bin` (ya da `firmware-cyd.bin`)
dosyasını kartın köküne kopyala, kartı tak, cihazı yeniden başlat. Dosya `update.bin` ya da
`firmware.bin` adında da olabilir. Kart yalnızca aynı modelin **daha yeni** sürümünü yükler;
işlediği dosyanın sonuna `.yuklendi`, `.eski` (aynı/eski sürüm) ya da `.bad` (yanlış dosya, örneğin
`ilk-kurulum-…bin`) ekler. İlk kurulum dosyası SD'den yüklenmez; o yalnızca USB ile yazılır.

## SD kart

Takılı değilse her şey yine çalışır. Takılıysa (FAT32) kart `/calisma/` klasörünü kullanır:

| Dosya | İçerik |
| --- | --- |
| `kuyruk.txt` | İnternet yokken biriken tik, erteleme ve kronometre kayıtları; Wi-Fi gelince buluta gider ve silinir |
| `kayit-AAAA-AA.txt` | Cihazda yapılan her şeyin aylık kalıcı kaydı (silinmez) |
| `plan.json` | Son alınan plan; açılışta internet beklemeden gösterilir |

Kart yokken kuyruk cihazın kendi hafızasında en fazla 40 kayıt tutar. Ayarlar ekranının alt
satırı SD'nin takılı olup olmadığını ve bekleyen kayıt sayısını gösterir.

## Telefonla eşleşme nasıl çalışır?

Ayrıntılar: [docs/firebase.md](../docs/firebase.md).

- Kart ve telefon `device/` altını canlı dinler: değişiklik saniyesinde gelir. Bağlantı koparsa
  kart planı 30 saniyede, kronometreyi 10 saniyede bir sorar. Ayarlar'da "(canlı)" yazısı
  akışın açık olduğunu gösterir.
- Telefon günün planını `device/view` altına yazar.
- Kart tik, erteleme ve kronometre oturumlarını `device/events` altına gönderir; telefon
  uygulaması bunları işler ve siler. Kart kendi değişikliklerini beklemeden hemen gösterir.
- Ortak kronometre `device/timer` altındadır. Bitiren taraf oturumu kaydeder, süre iki kez sayılmaz.
- İnternet yoksa olaylar SD'de (yoksa cihaz hafızasında) saklanır, bağlantı gelince gönderilir.
- Kart dakikada bir `device/status` yazar; telefondaki "Masa saati bağlı" yazısı buradan gelir.

## Geliştirme notları

- PlatformIO, `espressif32@6.9.0` (Arduino core 2.0.x). Ortamlar: `cyd` (ILI9341), `cyd_st7789`.
- Dokunmatik (XPT2046) yazılımsal SPI ile okunur; donanım VSPI SD kartındır.
- Türkçe harfler için yazı tipleri `tools/make_fonts.py` ile üretilir (`src/fonts.h`):
  arayüz Inter, başlıklar ve büyük rakamlar Lora. İkisi de SIL Open Font License 1.1.
- Derleme ve sürüm yayınlama `.github/workflows/firmware.yml` ile GitHub'da yapılır.
