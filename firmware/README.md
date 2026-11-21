# DAXEN VANE · masa saati yazılımı (ESP32 CYD)

ESP32-2432S028R ("Cheap Yellow Display") için küçük bir işletim sistemi. Ekstra parça gerekmez:
kart, USB kablosu ve powerbank yeterli. SD kart isteğe bağlıdır ama notlar, kitaplar, fotoğraflar,
müzik ve kendi uygulamaların için gerekir.

<img src="onizleme.png" width="880" alt="Ekranların önizlemesi: açılış, ana ekran, uygulamalar, YKS, hava, saat, notlar, satranç, müzik, kartlar, Lua uygulaması, ayarlar">

Tasarım: "Obsidian Silver". Siyah zemin, grafit paneller, gümüş yazılar, tek vurgu rengi çelik
mavisi. Yazılar Inter, simgeler çizgi tabanlı ve vektörel. Ayarlar → Ekran → *Kapak rengini vurgu
yap* açıksa telefonda çalan şarkının kapağı vurgu rengini belirler.

## Ana ekran

Üstte tarih, bildirim sayısı, Bluetooth ve Wi-Fi. Ortada saat ve hava durumu, altında YKS'ye kalan
gün ve seri (alev), topuyla oynayan kedi, şimdiki ya da sıradaki oturum kartı ve alt çubuk:
YKS, Notlar, Müzik, Ayarlar, Uygulamalar.

Dokunma: dokun, **basılı tut** (bağlam menüsü), kaydır. Ekranın **sol kenarından sağa kaydırmak**
ve başlıktaki sol ok her uygulamada geri gider.

## Uygulamalar

| Uygulama | Ne yapar |
| --- | --- |
| **YKS** | Plan (dokun: tamamlandı, basılı tut: başlat / ertele), Sayaç (Pomodoro dahil, telefondaki sayaçla ortak), Özet |
| **Notlar** | Klavyeyle metin notu ve parmakla el yazısı notu (`/daxen/notlar`) |
| **Dosyalar** | SD kart: aç, yeniden adlandır, kopyala, kes, yapıştır, sil, klasör oluştur |
| **Ayarlar** | Wi-Fi (ağ listesi, şifre, 5 kayıtlı ağ), Bluetooth, Ekran (parlaklık, gece kısma, otomatik parlaklık, dokunmatik ayarı), Bulut hesabı, Depolama, Güncelleme, Sistem |
| **Müzik** | SD karttaki MP3'leri Bluetooth kulaklığa çalar (aşağıya bak) |
| **Medya** | Telefonda çalan şarkıyı yönetir |
| **Bildirimler** | Telefondan gelen bildirimler |
| **Hesap Mak.** | Bilimsel hesap makinesi: işlem önceliği, parantez, trigonometri (derece/radyan), Ans |
| **Saat** | Alarm (günler, erteleme), geri sayım, kronometre (turlar), dünya saatleri |
| **Hava** | Şimdi, 24 saatlik grafik, 7 günlük tahmin, şehir arama (Open-Meteo) |
| **Yapılacaklar** | Basit yapılacaklar listesi |
| **Oyunlar** | Blok Patlat, Yılan, 2048, Mayın Tarlası, Sudoku, Satranç (3 zorluk) |
| **Fotoğraflar** | SD'deki JPEG ve BMP resimleri (`/daxen/foto`, `/DCIM`, `/Resimler`, `/Pictures`) |
| **Okuyucu** | Metin kitapları, kaldığı sayfayı hatırlar (`/daxen/kitaplar`) |
| **Çizim** | 10 renkli çizim defteri (`/daxen/cizim`) |
| **Kartlar** | Aralıklı tekrarlı bilgi kartları: TYT matematik, geometri, fizik, kimya, edebiyat; kendi destelerin `/daxen/kartlar/*.txt` (`ön|arka` satırları) |
| **Işık ölçer** | Ortam ışığı ve 1 dakikalık grafik |
| **Aktarım** | Tarayıcıdan SD karta dosya yükleme ve indirme (aynı Wi-Fi ağında, ekrandaki adresi aç); sayfada ilerleme çubuğu, yükleme sürerken saat donmaz |
| **Sistem** | Sürüm, çalışma süresi, son açılış sebebi, bellek, sıcaklık |
| **Görevler** | Çekirdeğin süreçleri: servisler ve görevler, işlemci payı, yığın, bellek; görevleri sonlandırma |

**Kendi uygulamaların:** SD karttaki `/daxen/uygulamalar/*.lua` dosyaları Uygulamalar ekranında
görünür. Her biri ayrı bir süreç olarak çalışır; `-- arkaplan: evet` diyen uygulama çıkınca da sürer.
İlk açılışta örnekler ve `REHBER.txt` yazılır. Ayrıntılar: [docs/lua.md](../docs/lua.md).

## Çekirdek ve süreçler

Donanım zamanlayıcısı saniyede 1000 kez kesme üretir. Kesme, vakti gelen işi olduğunda çekirdeği
uyandırır; çekirdek o an çalışan düşük öncelikli süreci keser, servisleri öncelik sırasıyla çalıştırır
ve bir sonraki vakte kadar uyur. Böylece satranç motoru düşünürken ya da bir Lua uygulaması uzun bir
hesap yaparken saat, dokunma ve alarmlar donmaz.

| Süreç | Tür | Periyot | İş |
| --- | --- | --- | --- |
| dokunma | servis | 10 ms | Dokunmatik ve BOOT tuşu |
| kabuk | servis | 16 ms | Ekran, pencereler, uygulamanın adımı |
| bluetooth | servis | 10 ms | Telefondan gelenler |
| saat | servis | 200 ms | Dakika işleri, Pomodoro, gece bakımı |
| ışık | servis | 50 ms | RGB ışık ve ekran parlaklığı |
| ağ | görev, çekirdek 0 | — | Wi-Fi, bulut, hava, güncelleme |
| web | görev, çekirdek 1 | — | Aktarım ve hesap sayfası sunucusu, yalnızca o ekran açıkken |
| lua:… | görev, çekirdek 1 | — | Açık (ya da arka plandaki) Lua uygulaması |
| satranç | görev, çekirdek 1 | — | Satranç motoru, yalnızca düşünürken |

Servisler çekirdek görevinde (öncelik 3) çalışır ve ekrana serbestçe çizer. Görevlerin kendi yığını
vardır (öncelik 1), ekrana yalnızca ekran kilidiyle çizer. Sonlandırma işbirliklidir: süreç isteği
görür ve kendisi çıkar, böylece yarım kalan SD yazması ya da tutulan bir kilit olmaz.

## Müzik: Bluetooth kulaklık ya da hoparlör

ESP32'nin belleği, Wi-Fi + telefon bağlantısı ile kulaklık sesini aynı anda kaldırmıyor. Bu yüzden
müzik ayrı bir yazılımdır: Müzik uygulamasında **Müziği aç**'a basınca cihaz müzik yazılımıyla
yeniden başlar, çıkınca DAXEN kaldığı yerden açılır.

1. MP3'leri SD kartta `/Muzik` klasörüne koy (Aktarım uygulamasıyla da olur).
2. İlk seferde Müzik → **Modülü indir** (Wi-Fi gerekir, yaklaşık 1,3 MB). Ya da Releases'taki
   `muzik-cyd_st7789.bin` dosyasını SD kartın köküne kopyala.
3. Müziği aç → üstteki çıkış simgesine dokun → **Hoparlör** ya da **Kulaklık** seç.
   Kulaklık için kulaklığı eşleşme kipine al, listeden seç. Hoparlör kartın `SPEAK` soketine
   (2 pinli, 1,25 mm) takılır; ses ESP32'nin 8 bitlik iç DAC'ından gelir (GPIO26), alarm ve
   bildirim için iyi, müzikte hafif cızırtılı. Kulaklık seçilmemişse hoparlörden başlar.

Müzik çalarken Wi-Fi, telefon bağlantısı ve bildirimler kapalıdır. Etiketsiz dosyalarda
`Sanatçı - Şarkı.mp3` adı kullanılır.

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

**Kablo:** veri taşıyan bir kablo olmalı. İki USB'li kartın USB-C girişinde gerekli dirençler yok,
bu yüzden **iki ucu USB-C olan kablo çalışmaz**. Telefondan USB-C → micro-USB kablo kullan ya da
USB-C → USB-A OTG çevirici takıp normal kablo bağla. Powerbank'te de USB-A çıkışını kullan.

## İlk açılış

1. **Dokunmatik ayarı:** 9 artının ortasına sırayla dokun, sonra 2 doğrulama noktasına. Sonuçta
   ortalama sapma piksel olarak görünür; yüksekse *Yeniden*. Tırnakla dokunmak daha hassastır.
   BOOT ile atlanabilir; sonradan Ayarlar → Ekran → *Dokunmatiği ayarla*.
2. **Wi-Fi:** kayıtlı ağ yoksa Ayarlar → Wi-Fi kendiliğinden açılır. Ağı seç, şifreyi ekrandaki
   klavyeyle yaz. Eski yazılımda kaydedilmiş ağ otomatik alınır.
3. **Bulut hesabı:** Ayarlar → Bulut hesabı. Alanları ekran klavyesiyle doldurabilir ya da
   *Tarayıcıdan gir*'e basıp telefonda ekrandaki adresi açabilirsin.
   Telefonla Bluetooth üzerinden eşleşiyorsan bu adım gerekmez.

**BOOT tuşu:** kısa bas → ana ekran, 2 saniye → Ayarlar, 6 saniye → Wi-Fi ayarları.

**Işık:** yeşil = sayaç çalışıyor, mavi yanıp sönme = 2 dk içinde oturum başlıyor ya da Pomodoro
molası, kırmızı = buluta bağlanamıyor. *Gece ekranı kıs* açıksa ekran 22:00–07:00 arası kısılır (Ayarlar → Ekran).
Otomatik parlaklık ortam ışığına göre ayarlar; sensör ters çalışıyorsa Işık ölçer → *Ters çevir*.

## Güncellemeler

Kart her açılışta, Bluetooth başlamadan önce GitHub'daki en yeni **fw-v…** sürümünü denetler,
varsa indirip kurar. Her gece 04:00–04:30 arasında, kimse kullanmıyorsa kendini yeniden başlatır.
Ayarlar → Güncelleme → *Güncellemeyi denetle* hemen denetler. Yeni sürüm açıldıktan 45 saniye sonra
sağlam sayılır; daha önce çökerse kart eski sürüme döner.

**SD karttan:** Releases'taki `firmware-cyd_st7789.bin` (ya da `firmware-cyd.bin`) dosyasını kartın
köküne kopyala, cihazı yeniden başlat. Dosya `update.bin` ya da `firmware.bin` adında da olabilir.
Kart yalnızca aynı modelin **daha yeni** sürümünü yükler; işlediği dosyanın sonuna `.yuklendi`,
`.eski` ya da `.bad` ekler. İlk kurulum dosyası SD'den yüklenmez.

## SD kart

Takılı değilse sistem yine çalışır (notlar, müzik, fotoğraf ve Lua uygulamaları hariç). FAT32 olmalı.

| Klasör | İçerik |
| --- | --- |
| `/calisma/` | Plan önbelleği, internet yokken biriken kayıtlar ve aylık kalıcı kayıt |
| `/daxen/notlar/` | Notlar (`.txt` metin, `.el` el yazısı) |
| `/daxen/kitaplar/` | Okuyucu için `.txt` kitaplar |
| `/daxen/foto/` | Fotoğraflar |
| `/daxen/cizim/` | Çizimler |
| `/daxen/kartlar/` | Kendi bilgi kartı destelerin |
| `/daxen/uygulamalar/` | Lua uygulamaları ve verileri |
| `/daxen/sys/` | Müzik yazılımı gibi sistem dosyaları |
| `/Muzik/` | MP3'ler |

## Telefonla eşleşme

Bluetooth: [docs/bluetooth.md](../docs/bluetooth.md). Bulut (Firebase): [docs/firebase.md](../docs/firebase.md).
Bluetooth bağlıyken Wi-Fi gerekmez: plan, tikler, sayaç, saat, bildirimler ve medya telefondan gelir.

## Geliştirme notları

- PlatformIO, `espressif32@6.9.0` (Arduino core 2.0.x).
  Ortamlar: `cyd`, `cyd_st7789` (ana yazılım) ve `muzik_cyd`, `muzik_cyd_st7789` (müzik yazılımı).
- `src/main.cpp` sistem hizmetleri (ağ, bulut, Bluetooth, güncelleme) ve servislerin kaydı;
  `src/os/kernel.h` zamanlayıcı kesmesi ve süreç zamanlayıcı; `src/daxen.h` arayüzü toplar:
  `core/` ortak durum ve yardımcılar, `ui/` tasarım sistemi (renkler, simgeler, bileşenler, klavye),
  `os/` çekirdek ve kabuk (ana ekran, başlatıcı, dokunma), `apps/` uygulamalar. Yeni uygulama: `apps/` altına
  bir `App` tanımı yaz, `os/shell.h` içindeki `AppId` ve `os/registry.h` tablosuna ekle.
- Renkler `ui/theme.h`, simgeler `ui/icons.h` (48'lik ızgarada vektör), logo `tools/make_logo.py`,
  yazı tipleri `tools/make_fonts.py` (Inter, SIL Open Font License 1.1).
- Müzik yazılımı `src/music/` (ESP32-A2DP ve libhelix), Lua 5.4 `lib/lua/` (MIT).
- Dokunmatik: `core/touch.h` (XPT2046, eksen başına 5 ölçümün ortancası), `core/calib.h`
  (9 noktadan en küçük karelerle afin dönüşüm: ölçek, kayma, eksen takası, dönüklük), `os/calibrate.h`.
- **Testler:** `test/host/run.sh test` gerçek kodu bilgisayarda AddressSanitizer ve
  UndefinedBehaviorSanitizer ile derler; birim testlerini (satranç perft, hesap makinesi, kalibrasyon,
  sudoku, Lua, Türkçe metin, çekirdek) ve bütün ekran senaryolarını çalıştırır. GitHub'da her derlemeden
  önce çalışır; geçmezse sürüm çıkmaz. `run.sh preview` ekran görüntülerini `test/host/out/` klasörüne yazar.
- Derleme ve sürüm yayınlama `.github/workflows/firmware.yml` ile GitHub'da yapılır.
