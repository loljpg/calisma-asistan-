# DAXEN VANE · masa saati yazılımı (ESP32 CYD)

ESP32-2432S028R ("Cheap Yellow Display") için küçük bir işletim sistemi. Ekstra parça gerekmez:
kart, USB kablosu ve powerbank yeterli. SD kart isteğe bağlıdır ama notlar, kitaplar, fotoğraflar
ve kendi uygulamaların için gerekir.

<img src="onizleme.png" width="880" alt="Ekranların önizlemesi: açılış, ana ekran, uygulamalar, YKS, hava, saat, notlar, satranç, medya, kartlar, Lua uygulaması, ayarlar">

Tasarım: "Obsidian Silver". Siyah zemin, grafit paneller, gümüş yazılar, tek vurgu rengi çelik
mavisi. Yazılar Inter, simgeler çizgi tabanlı ve vektörel. Ayarlar → Ekran → *Kapak rengini vurgu
yap* açıksa telefonda çalan şarkının kapağı vurgu rengini belirler.

## Ana ekran

Üstte tarih ve sistem alanı: bildirim sayısı, Bluetooth, Wi-Fi ve arka planda süren işler
(Aktarım, arka plandaki Lua uygulaması, ağ ya da Bluetooth taraması, kronometre, telefondaki
medya). Ortada saat ve hava durumu, altında YKS'ye kalan
gün ve seri (alev), topuyla oynayan kedi, şimdiki ya da sıradaki oturum kartı ve alt çubuk:
YKS, Notlar, Ayarlar, Uygulamalar.

Dokunma: dokun, **basılı tut** (bağlam menüsü), kaydır. Ekranın **sol kenarından sağa kaydırmak**
ve başlıktaki sol ok her uygulamada geri gider.

**Güvenlik:** Ayarlar → Güvenlik'te PIN, ekran kilidi, geliştirici seviyesi (Terminal'in
yönetim komutları, 10 dk) ve ayar yedeği. Bulut girişi Google'ın kökleriyle doğrulanır, şifre
saklanmaz. Ayrıntılar ve sınırlar: [docs/guvenlik.md](../docs/guvenlik.md).

**Sistem menüsü:** her ekranda üst kenardan aşağı kaydır ya da ana ekranın durum çubuğuna dokun.
Saat ve boş bellek; Wi-Fi, telefon, SD kart ve bulut kutucukları (dokununca ilgili ayar sayfası);
parlaklık; son kullanılan dört uygulama (hızlı geçiş) ve arka planda süren işler; son bildirim
(dokununca Bildirimler); Terminal, Görevler ve onaylı yeniden başlatma. Yukarı kaydırınca, üst
satıra dokununca ya da BOOT'a basınca kapanır. Menü açıkken alttaki uygulama çalışmaya devam eder.

## Uygulamalar

| Uygulama | Ne yapar |
| --- | --- |
| **YKS** | Plan (dokun: tamamlandı, basılı tut: başlat / ertele), Sayaç (Pomodoro ve odak modu dahil, telefondaki sayaçla ortak), Soru (günlük çözülen soru, derse göre, hedef, son 14 gün), Özet (Çalışma serisi → son 12 haftanın ısı haritası) |
| **Notlar** | Klavyeyle metin notu ve parmakla el yazısı notu (`/daxen/notlar`) |
| **Dosyalar** | SD kart: aç, yeniden adlandır, kopyala, kes, yapıştır, sil, klasör oluştur |
| **Ayarlar** | Wi-Fi (ağ listesi, şifre, 5 kayıtlı ağ), Bluetooth, Ekran (parlaklık, gece kısma, otomatik parlaklık, dokunmatik ayarı), Bulut hesabı, Depolama, Güncelleme, Sistem |
| **Medya** | Telefonda çalan şarkıyı gösterir ve yönetir (kart ses çalmaz) |
| **Bildirimler** | Telefondan gelen bildirimler |
| **Hesap Mak.** | Bilimsel hesap makinesi: işlem önceliği, parantez, trigonometri (derece/radyan), Ans |
| **Saat** | Alarm (günler, erteleme), geri sayım, kronometre (turlar), dünya saatleri |
| **Hava** | Şimdi, 24 saatlik grafik, 7 günlük tahmin, şehir arama (Open-Meteo) |
| **Yapılacaklar** | Basit yapılacaklar listesi |
| **Oyunlar** | Blok Patlat, Yılan, 2048, Mayın Tarlası, Sudoku, Satranç (3 zorluk) |
| **Fotoğraflar** | SD'deki JPEG ve BMP resimleri (`/daxen/foto`, `/DCIM`, `/Resimler`, `/Pictures`) |
| **Okuyucu** | Metin kitapları, kaldığı sayfayı hatırlar (`/daxen/kitaplar`) |
| **Çizim** | Vektör çizim: kalem (titreme sabitleyicili), çizgi, dikdörtgen, elips (boş/dolu), yazı, silgi, damlalık; 20 renk, 1-20 px; geri al/yinele; ayna, kılavuz (şekiller ızgaraya yaslanır), kağıt rengi; resim olarak kaydetme (`/daxen/foto/*.bmp`); küçük resimli liste (`/daxen/cizim`) |
| **Kartlar** | Aralıklı tekrarlı bilgi kartları: TYT matematik, geometri, fizik, kimya, edebiyat; kendi destelerin `/daxen/kartlar/*.txt` (`ön|arka` satırları) |
| **Işık ölçer** | Ortam ışığı ve 1 dakikalık grafik |
| **Mağaza** | Yazılımla gelen isteğe bağlı Lua uygulamaları (Deneme: net hesabı ve gelişim grafiği; Nefes: yönlendirmeli nefes; Pong). Varlık paketinde durur, flash'ta yer tutmaz; Kur / Güncelle, basılı tut: kaldır |
| **Aktarım** | Tarayıcıdan SD karta dosya yükleme ve indirme (aynı Wi-Fi ağında, ekrandaki adresi aç ve ekrandaki 6 haneli kodla gir); sayfada ilerleme çubuğu, yükleme sürerken saat donmaz. Tarayıcıda metin dosyalarını düzenleme (`.txt`, `.md`, `.lua`, `.csv`, `.json`, `.ini`; 64 KB'a kadar, sistem klasörleri hariç), yeni metin dosyası, Yapılacaklar listesi (ekle, işaretle, sil) |
| **Sistem** | Sürüm, çip, çalışma süresi, son açılış sebebi (önceki çökme kaydı varsa belirtilir), bellek (boş, en az, en büyük blok), Wi-Fi adresi ve sinyali |
| **Görevler** | Çekirdeğin süreçleri: servisler ve görevler, işlemci payı, yığın, bellek; görevleri sonlandırma. Wi-Fi, TCP/IP ve Bluetooth sistem görevleri de görünür (yığın ve durum; işlemci payı ölçülemez, "—") |
| **Ağ** | Bağlı Wi-Fi'nin ayrıntıları (sinyal, BSSID, kanal, adres, ağ geçidi, DNS) ve bağlantı testi (ağ geçidi, DNS, internet); çevredeki erişim noktalarının taranması (her BSSID ayrı, kanal, güvenlik); yakındaki Bluetooth cihazları (yalnız reklam paketleri: ad, adres türü, sinyal, üretici, servisler; dokununca ayrıntı) |
| **Terminal** | Kendi komut yorumlayıcısı (Linux değil): `help`, `sysinfo`, `memory`, `tasks`, `logs`, `storage`, `apps`, `app launch`, `wifi status/ip/saved/scan/ping/dns/tcp/traceroute/test`, `stop`, `bluetooth status/scan/devices/info`, `lua list/info/log/remove/stop`, `dev`, `backup`, `restore`, `logs clear`, `version`, `uptime`, `history`, `clear`, `reboot`. USB seri porttan da (115200) çalışır. Ayrıntılar: [docs/terminal.md](../docs/terminal.md) |

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
| wifi, tcp/ip, bt-host, bt-denetleyici | sistem görevi | — | ESP-IDF ve NimBLE'nin kendi görevleri (yalnızca izlenir) |
| web | görev, çekirdek 1 | — | Aktarım ve hesap sayfası sunucusu, yalnızca o ekran açıkken (üstünde başka ekran açık kalırsa 10 dk istek gelmeyince kapanır) |
| ağ-araç | görev, çekirdek 1 | — | Terminal ağ araçları (ping, dns, tcp, traceroute, test), iş bitince kapanır |
| lua:… | görev, çekirdek 1 | — | Açık (ya da arka plandaki) Lua uygulaması |
| satranç | görev, çekirdek 1 | — | Satranç motoru, yalnızca düşünürken |

Servisler çekirdek görevinde (öncelik 3) çalışır ve ekrana serbestçe çizer. Görevlerin kendi yığını
vardır (öncelik 1), ekrana yalnızca ekran kilidiyle çizer. Sonlandırma işbirliklidir: süreç isteği
görür ve kendisi çıkar, böylece yarım kalan SD yazması ya da tutulan bir kilit olmaz.

Ağır işler (Lua, web sunucusu, ağ aracı, satranç motoru, Bluetooth taraması) başlamadan önce bellek
bekçisine sorar: 18 KB taban korunur, aynı anda en çok 3 ağır iş çalışır. Ayrıntı ve sayılar:
[docs/bellek.md](../docs/bellek.md).

**Odak modu** (YKS → Sayaç): telefondan gelen bildirimler afiş olarak çıkmaz (Bildirimler'de durur),
Oyunlar açılmaz; 3 saat sonra kendiliğinden biter. Sistem alanında "Odak" görünür.

**Soru sayacı** (YKS → Soru): +1/+5/+10/+20 ve −1 düzeltme; ders seçici (kronometredeki ders varsayılan).
Sayılar NVS'de; her değişiklik birkaç saniye sonra `soru` olayı olarak buluta ve SD'deki kalıcı kayda
gider (telefondaki uygulama bu olayı şimdilik yok sayar). Çalışma geçmişi SD'deki aylık kalıcı
kayıttaki kronometre oturumlarından hesaplanır (telefonda tutulan süreler dahil değildir).

## Ses

Kart ses çalmaz. SD karttan müzik çalma (ayrı müzik yazılımı, Bluetooth kulaklık ve hoparlör)
kaldırıldı. SD karttaki şarkılara dokunulmaz; SD'de kalan eski müzik modülü dosyası
(`/daxen/sys/muzik-*.bin`) için bir kez "silinsin mi?" diye sorulur. `SPEAK` soketindeki
yükseltecin girişi (GPIO26) açılışta LOW tutulur, takılı bir hoparlör vızıldamaz.

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

**Açılışta BOOT** (logo ekranındaki 2 saniye): kısa bas → dokunmatik ayarı, 3 saniye basılı tut →
**kurtarma kipi**. Art arda 3 çökmeden sonra kurtarma kipi kendiliğinden açılır. Seçenekler: normal
başlat, güvenli başlat (Wi-Fi kapalı), PIN'i ve ekran kilidini kaldır, dokunmatik ayarını sıfırla,
önceki yazılıma dön, bütün ayarları sıfırla. Silen ya da değiştiren seçenek ikinci kez seçilince
uygulanır; SD karttaki dosyalara hiçbiri dokunmaz.

**Işık:** yeşil = sayaç çalışıyor, mavi yanıp sönme = 2 dk içinde oturum başlıyor ya da Pomodoro
molası, kırmızı = buluta bağlanamıyor. *Gece ekranı kıs* açıksa ekran 22:00–07:00 arası kısılır (Ayarlar → Ekran).
Otomatik parlaklık ortam ışığına göre ayarlar; sensör ters çalışıyorsa Işık ölçer → *Ters çevir*.

## Güncellemeler

Kart her açılışta, Bluetooth başlamadan önce GitHub'daki en yeni **fw-v…** sürümünü denetler,
varsa indirip kurar. Her gece 04:00–04:30 arasında, kimse kullanmıyorsa kendini yeniden başlatır.
Ayarlar → Güncelleme → *Güncellemeyi denetle* hemen denetler. Yeni sürüm açıldıktan 45 saniye sonra
sağlam sayılır; daha önce çökerse kart eski sürüme döner.

GitHub bağlantılarının sertifikaları kartta gömülü 8 kök sertifikayla doğrulanır
(`certs/ota-roots.pem` → `src/sys/ota_roots.h`, `tools/make_ota_roots.py`). CI her derlemede GitHub'ın
gerçek zincirlerini kartın algoritmasıyla dener (`tools/check_ota_chains.py`); tutmazsa yayın yapılmaz.
Doğrulama kartta başarısız olursa güncelleme kurulmaz ve sebep günlüğe yazılır; SD karttan güncelleme
her zaman çalışır. Kart saati denetlenmez (sertifika tarihleri kartta karşılaştırılmaz).

**SD karttan:** Releases'taki `firmware-cyd_st7789.bin` (ya da `firmware-cyd.bin`) dosyasını kartın
köküne kopyala, cihazı yeniden başlat. Dosya `update.bin` ya da `firmware.bin` adında da olabilir.
Kart yalnızca aynı modelin **daha yeni** sürümünü yükler; işlediği dosyanın sonuna `.yuklendi`,
`.eski` ya da `.bad` ekler. İlk kurulum dosyası SD'den yüklenmez.

## SD kart

Takılı değilse sistem yine çalışır (notlar, fotoğraf ve Lua uygulamaları hariç). FAT32 olmalı.

**Yazı tipleri, logo ve Lua dosyaları SD karttadır** (`/daxen/sys/varlik.bin`, yazılıma gömülü
değil; bellekte yer açmak için). Paket yoksa ya da yazılımın beklediği sürüm değilse kart onu
Wi-Fi'deyken çalıştığı sürümün GitHub sayfasından indirir (açılışta, sonra dakikada bir dener);
indirme sertifika doğrulamalıdır, dosya boyu, sürümü ve CRC32'si tutmazsa kullanılmaz. Paket
inene kadar ya da SD kart yokken ekran yerleşik yazıyla çalışır (ç → c, ğ → g …).

| Klasör | İçerik |
| --- | --- |
| `/calisma/` | Plan önbelleği, internet yokken biriken kayıtlar ve aylık kalıcı kayıt |
| `/daxen/notlar/` | Notlar (`.txt` metin, `.el` el yazısı) |
| `/daxen/kitaplar/` | Okuyucu için `.txt` kitaplar |
| `/daxen/foto/` | Fotoğraflar |
| `/daxen/cizim/` | Çizimler |
| `/daxen/kartlar/` | Kendi bilgi kartı destelerin |
| `/daxen/uygulamalar/` | Lua uygulamaları ve verileri (her uygulamanın günlüğü `veri/<ad>/gunluk.txt`); izinler ilk açılışta sorulur ([docs/lua.md](../docs/lua.md)) |
| `/daxen/sys/` | Sistem dosyaları; `varlik.bin` yazı tipleri, logo, Lua dosyaları; `gunluk.txt` sistem günlüğü (32 KB'ta `gunluk.1.txt`'ye döner) |
| `/daxen/gizli/` | Gizli özelliklerin açıldığı yer ([docs/gizli.md](../docs/gizli.md)) |

## Telefonla eşleşme

Bluetooth: [docs/bluetooth.md](../docs/bluetooth.md). Bulut (Firebase): [docs/firebase.md](../docs/firebase.md).
Bluetooth bağlıyken Wi-Fi gerekmez: plan, tikler, sayaç, saat, bildirimler ve medya telefondan gelir.

## Geliştirme notları

- PlatformIO, `espressif32@6.9.0` (Arduino core 2.0.x).
  Ortamlar: `cyd` (ILI9341) ve `cyd_st7789`.
- `src/main.cpp` açılış ve çekirdek servislerinin kaydı; `src/sys/` sistem katmanı (kalıcı ayarlar,
  SD kart, ağ ve bulut, Bluetooth, web sunucusu, ışıklar, sistem hizmetleri);
  `src/os/kernel.h` zamanlayıcı kesmesi ve süreç zamanlayıcı; `src/daxen.h` arayüzü toplar:
  `core/` ortak durum ve yardımcılar, `ui/` tasarım sistemi (renkler, simgeler, bileşenler, klavye),
  `os/` çekirdek ve kabuk (ana ekran, başlatıcı, dokunma), `apps/` uygulamalar. Yeni uygulama: `apps/` altına
  bir `App` tanımı yaz, `os/shell.h` içindeki `AppId` ve `os/registry.h` tablosuna ekle.
- Renkler `ui/theme.h`, simgeler `ui/icons.h` (48'lik ızgarada vektör), logo `tools/make_logo.py`,
  yazı tipleri `tools/make_fonts.py` (Inter, SIL Open Font License 1.1) → `assets/`. Varlık paketi
  `tools/make_assets.py` (`assets/`, `lua/` → `varlik.bin` ve beklenen sürüm `src/core/varlik.h`;
  `--check` ile denetler); okuma `ui/font.h`, indirme `sys/assets.h`. Lua dosyası, yazı tipi ya da
  logo değişince `make_assets.py` çalıştırılıp `varlik.h` işlenmeli, yoksa CI geçmez. Mağaza
  uygulamaları `lua/magaza/*.lua` (dosya adı en çok 16 bayt; başlıkta `-- aciklama:` satırı).
- Lua 5.4 `lib/lua/` (MIT).
- Dokunmatik: `core/touch.h` (XPT2046, eksen başına 5 ölçümün ortancası), `core/calib.h`
  (9 noktadan en küçük karelerle afin dönüşüm: ölçek, kayma, eksen takası, dönüklük), `os/calibrate.h`.
- **Testler:** `test/host/run.sh test` gerçek kodu bilgisayarda AddressSanitizer ve
  UndefinedBehaviorSanitizer ile derler; birim testlerini (satranç perft, hesap makinesi, kalibrasyon,
  sudoku, Lua, Türkçe metin, çekirdek, ayar geçişleri) ve bütün ekran senaryolarını çalıştırır. GitHub'da her derlemeden
  önce çalışır; geçmezse sürüm çıkmaz. `run.sh preview` ekran görüntülerini `test/host/out/` klasörüne yazar.
- Derleme ve sürüm yayınlama `.github/workflows/firmware.yml` ile GitHub'da yapılır.
