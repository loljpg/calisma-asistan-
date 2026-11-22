# CYD Infinity Engine (Infinity uygulaması)

Kartın işlemcisini, ekranını ve belleğini gerçek iş yükleriyle ölçen kıyas motoru. Ana görüntü sonsuz
yakınlaşan Mandelbrot kümesidir; her nokta kartta, o an hesaplanır (hazır görüntü ya da kayıtlı kare yok).
Uygulamalar → **Infinity**.

Kod: `firmware/src/apps/bench.h` (uygulama, işçiler, testler, arayüz), `firmware/src/core/benchcalc.h`
(saf hesap: çekirdekler, renk, yakınlaşma yolu, istatistik; bilgisayarda sınanır).

## Donanım (depodan belirlenen)

| | Değer | Kaynak |
| --- | --- | --- |
| İşlemci | ESP32, iki Xtensa LX6 çekirdeği | `platformio.ini`: `board = esp32dev`; çip modeli ve sürümü çalışırken okunur |
| Saat | 240 MHz (Arduino kart ayarı) | çalışırken `ESP.getCpuFreqMHz()` ile okunup rapora yazılır |
| Flash | 4 MB, uygulama bölümü 1,9 MB | `min_spiffs.csv` |
| PSRAM | yapılandırılmamış | `esp32dev`'de `BOARD_HAS_PSRAM` yok; çalışırken `ESP.getPsramSize()` denetlenir, 0 ise "yok" yazılır |
| Ekran | ILI9341 (`cyd`) ya da ST7789 (`cyd_st7789`), 320×240 yatay | `platformio.ini` sürücü bayrakları |
| Ekran yolu | HSPI: MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, ışık 21; istenen saat 55 MHz | `platformio.ini` |
| Gerçek SPI saati | ESP32 SPI saati 80 MHz'in tam bölenidir: 55 MHz istenince **40 MHz** olur | ekran testi yazmacı okuyup rapora yazar |
| Dokunmatik | XPT2046, yazılımla SPI (ayrı bacaklar) | `src/core/touch.h` |
| SD kart | VSPI (ekrandan ayrı yol) | |
| Görev bekçisi | 5 sn, çekirdek 0'ın boşta görevini izler, tetiklenirse kart yeniden başlar; kesme bekçisi 300 ms | Arduino-ESP32 2.0.17 hazır SDK yapılandırması |
| Kütüphaneler | TFT_eSPI 2.5.43, Arduino-ESP32 2.0.17 (ESP-IDF 4.4), `espressif32@6.9.0` | `platformio.ini` |

CYD kartları arasında farklar olabilir (ekran yongası, PSRAM). Infinity donanımı varsaymaz: saat, PSRAM,
SPI saati ve bellek çalışırken ölçülür ve raporda yazar.

## Mimari

```
çekirdek 0                     çekirdek 1
┌──────────────┐   ┌──────────────┐   ┌───────────────────────────────┐
│ işçi kiyas-0 │   │ işçi kiyas-1 │   │ çekirdek görevi (arayüz, 16 ms)│
│ bant al      │   │ bant al      │   │ hazır bantları sırayla yolla   │
│ hesapla      │   │ hesapla      │   │ (pushImage, süre ölçülür)      │
│ → HAZIR      │   │ → HAZIR      │   │ sonraki kareyi hazırla, denetle│
└──────┬───────┘   └──────┬───────┘   └──────────────┬────────────────┘
       └──── 4 bant ara belleği (320 × 8 satır, 16 bit) ─────┘
```

- Kare, 8 satırlık bantlara bölünür. İşçiler bantları sırayla alır (tek bir `portMUX` kilidiyle yalnızca
  sayaç ve yuva seçimi; hesap kilitsiz). Arayüz hazır bantları sırasıyla ekrana yollar. Bant sırası
  korunduğu için kilitlenme olmaz; işçiler bir kare önden gidebilir (aktarım sürerken sonraki kare hesaplanır).
- Renk tablosu ekranın bayt sırasında tutulur; gönderirken bayt çevirme yapılmaz.
- İşçiler 25 ms'den uzun kesintisiz hesap yapmaz: `vTaskDelay(1)` ile boşta görevlerine yer açar (görev
  bekçisi ve silinen görevlerin belleği). Bekleme süresi hesap süresinden düşülür.
- Bellek yalnızca hesap sürerken ayrılır. Boştayken ve uygulama kapanınca bantlar, işçi yığınları ve
  bağlam geri verilir. BAŞLA'ya basınca önce bulut akışı bırakılır; en iyi düzen (4 bant, 2 işçi) sığana
  dek en çok 3 sn beklenir. Yine sığmazsa bantlar incelir (4 × 4 satır), sayısı azalmaz: iki işçi
  hesaplarken iki bant gönderilmeyi bekleyebilsin. İki işçi (iki çekirdek) her zaman tek işçiden önce
  gelir (`docs/bellek.md`, ağır işler). Raporda "İşçi" satırı kullanılan düzeni yazar.
- Hesap sürerken kabuk turu 16 ms yerine 4 ms'dir (`kSetPeriod`): hazır bant en geç 4 ms'de gönderilir,
  işçiler boş yuva beklemez. Bitince 16 ms'ye döner.
- Test sürerken bulutun canlı akışı duraklatılır (`netHold`), test bitince kendiliğinden döner: hem şifreli
  bağlantının tuttuğu bellek boşalır hem çekirdek 0 ölçüme karışmaz.

### Hesap

| Katman | Sayı | Ne zaman |
| --- | --- | --- |
| Sığ | float (donanım FPU) ya da sabit nokta (ayar) | piksel adımı ≥ 4·10⁻⁷ |
| Orta | sabit nokta Q4.28 (32 bit, çarpımlar 64 bit) | piksel adımı ≥ 4·10⁻⁸ |
| Derin | double (yazılımla; yalnızca Aşırı yük) | piksel adımı ≥ 10⁻¹³ |

- float çekirdeği iki komşu noktayı aynı döngüde yürütür: ESP32'nin FPU'sunda her işlemin sonucu birkaç
  saat beklettiği için iki bağımsız zincir bu boşlukları doldurur. Biri kaçınca öteki tek başına sürer;
  sonuç nokta nokta tek yörüngeyle aynıdır (testlerle sınanır).
- Ana kardioid ve 2. dönem diski iterasyonsuz atlanır (görüntüde; işlemci testinde kapalı).
- Yumuşak renk: kaçış sayısının kesirli hâli (`it + 2 − log2(log2|z|²)`), hızlı log2 yaklaşığı, 256 renkli
  palet tablosu. Renk akışı karenin sırasına bağlıdır (her çalıştırmada aynı).
- Yakınlaşma yolu: beş bilinen sınır noktası (Denizatı vadisi, Misiurewicz M4,1, Feigenbaum noktası,
  c = i, sarmal). Her kare sabit oranda büyür (yavaş ×0,985, orta ×0,97, hızlı ×0,94); katman bitince ya
  da kare tek renge dönünce sonraki hedefe geçilir. Hedef değişimi n−2. kareye bakar, böylece kare dizisi
  hesap hızından bağımsızdır.
- İterasyon sınırı derinlikle büyür: `taban × (1 + log2(büyütme)/12)`, en çok 4096. Taban: düşük 48,
  orta 96, yüksek 192, aşırı 384; Aşırı yük modunda iki katı.

## Modlar

| | Maks. performans | Aşırı yük |
| --- | --- | --- |
| Çözünürlük | 160×88 nokta (2×2 blok) → 320×176 | 320×176 tam |
| Aritmetik | float / sabit nokta | + double (derin) |
| İterasyon | taban | taban × 2 |
| Bellek baskısı | yok | boş bellek tabana 12 KB kalana dek 1 KB'lık bloklar tutulur; her denetimde ikisi bırakılıp yeniden alınır (ayırma süresi ve başarısızlık sayılır) |

## Pano

Alt şerit (tam ekran görünümünde dokunarak sayfa değişir): **kare** (FPS anlık, ortalama FPS, son kare
süresi), **işlem** (Mpx/s, Mit/s, iterasyon sınırı), **bellek** (boş, en az, en büyük blok), **derinlik**
(süre, büyütme, aritmetik). Fraktala dokunmak pano görünümüne geçer (fraktal 320×112, altında 12 değer:
FPS anlık/ort/aralık, kare/hesap/aktarım süresi, Mpx/s, Mit/s, ekran Mpx/s, boş/en az/en büyük blok ve
parçalanma). Şerit saniyede 2, pano 1 kez yenilenir; yalnız değişen değer, titremeden (ara bellekte)
yazılır. Test sürerken rakamların glifleri bellekte tutulur (`fontCacheOn`), her harf için SD karta
inilmez. Bu çizimin süresi de ölçülür ve rapora "Arayüz çizimi" olarak girer.

Dokunmalar: alt solda mod (MAKS / AŞIRI), alt sağda BAŞLA / DUR; başlıkta Ayarlar (karmaşıklık,
yakınlaşma hızı, palet, süre; Gelişmiş: sığ katman aritmetiği, bellek testi sınırı, varsayılanlar) ve
Testler (son sonuçlar, işlemci, ekran, bellek, tam test, görüntüyü sıfırla).

## Ölçülen, hesaplanan, kuramsal

Raporda her değerin yanında kaynağı yazar: **(ö)** doğrudan ölçüldü, **(h)** ölçülen değerlerden
hesaplandı, **(k)** kuramsal (karşılaştırma için).

- **FPS** yalnızca ekrana tamamen yazılmış karelerden sayılır (`kare / süre`). En az ve en çok FPS, ardışık
  iki tamamlanan kare arasındaki en uzun ve en kısa süreden gelir.
- **Hesap / kare**: bantların hesap süreleri toplamı (iki işçinin toplamı; yield beklemeleri düşülür).
  Çekirdek 1'deki işçi ekran aktarımı sırasında kesildiği için bu değer aktarımla biraz örtüşebilir; saf
  hesap hızı işlemci testindedir.
- **Ekran aktarımı / kare**: `pushImage` çağrılarının süresi (yalnızca yollama).
- **Mpx/s**: hesaplanan nokta (yarım çözünürlükte 4 ekran pikseline 1 nokta); **ekran Mpx/s**: ekrana
  yazılan piksel. **Mit/s**: sayılan iterasyonlar (z ← z² + c); en çok değer 1 saniyelik pencereden.
- **Parçalanma** = 1 − en büyük blok / boş bellek (h).
- İşlemci sıcaklığı ve "işlemci kullanımı" gösterilmez: ESP32'nin iç sıcaklık algılayıcısı belgelenmemiş
  ve güvenilir değil; FreeRTOS çalışma süresi istatistikleri hazır SDK'da kapalı
  (`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`). Uydurma değer yazılmaz.

## Testler

**İşlemci** (ekran durgun, sabit yük): görüntü (−0,65; 0), genişlik 3. float ve sabit nokta 320×240 nokta,
sınır 256; double 160×120 nokta, sınır 128. Kardioid testi kapalı (saf iterasyon hızı). Her aritmetik
üç kez: yalnız çekirdek 1, yalnız çekirdek 0, iki çekirdek birlikte. Rapor: süre, Mit/s, hızlanma
(tek çekirdek 1 süresi / iki çekirdek süresi) ve verim (hızlanma / 2), görev başlama gecikmesi, iki işçinin
bitiş farkı, `portMUX` giriş + çıkış süresi (20 000 kez). Bantlar çekirdeklere nasıl dağılırsa dağılsın
toplam iterasyon aynıdır (testlerle sınanır), yani iş miktarı her yapılandırmada eşittir.

**Ekran** (bütün ekran kullanılır, sonra yeniden çizilir): SPI saati (yazmaçtan), tam ekran doldurma ×10,
300 rastgele dikdörtgen (16-96 px), 20 000 tek piksel, 320×8'lik bantlarla tam ekran görüntü ×4, 64×40'lık
kısmi parça ×100. Rastgele konumlar sabit tohumlu üreteçle: her çalıştırmada aynı.

**Bellek**: boş, en az, en büyük blok, boş blok sayısı, PSRAM; ayar sınırına (32/64/96 KB) ve güvenli
sınıra (boş − 34 KB) kadar 4 KB'lık bloklar ayrılır (süre), desenle doldurulur (yazma hızı), okunup
doğrulanır (okuma hızı, hatalı sözcük sayısı), bırakılır (süre, boş bellek farkı). Sonra 24 × 1 KB ayrılıp
tekleri bırakılır (parçalanma), hepsi bırakılır (toparlanma). Güvenli pay yoksa adım atlanır.

## Güvenlik

- Boş bellek 18 KB tabana 6 KB yaklaşınca bellek baskısı blokları hemen bırakılır; 20 KB'ın altına inerse
  test durur ("bellek kritik"). Bellek testi tabanın 16 KB üstünde kalır.
- İşçi görev biterse (ör. Görevler'den sonlandırılırsa) ya da 20 sn hiç ilerleme olmazsa test durur.
- İşçiler en geç 25 ms'de bir boşa çıktığı için görev bekçisi açık kalır ve tetiklenmez; kilit bölgeleri
  mikrosaniyelik olduğu için kesme bekçisi de. Hiçbir koruma kapatılmaz, saat ve gerilim değiştirilmez.
- Menü ya da sistem penceresi açık kaldığı süre (arayüz turu 250 ms'den uzun durursa) test süresinden ve
  kare aralığından düşülür; raporda "duraklama" olarak yazar.
- Test sürerken ekran kilidi devreye girmez (son dokunuş tazelenir); test bitince normale döner.
- Bildirim balonu altta açıkken fraktalın oraya düşen satırları yazılmaz; sayısı raporda.

## Rapor

Her testin sonunda sonuç sayfası açılır (kaydırarak ya da `<` `>` ile sayfalar). Rapor
`/daxen/kiyas/YYYYAAGG-SSDDss.txt` dosyasına (saat yoksa `kiyas-<sn>.txt`) ve seri porta (`[kiyas]`
satırları, 115200) yazılır; "Raporu aç" Okuyucu'da açar. İçerik: mod ve ayarlar, süre, kare sayısı,
FPS ort/en az/en çok, ortalama kare süresi, hesap ve aktarım süresi, Mpx/s, Mit/s (ort ve en çok), bant
payları, en derin büyütme, bellek (başta, en az, en büyük blok, parçalanma, baskı), kararlılık (nasıl
bittiği), kart, saat, PSRAM ve yazılım yapısı.

## Tekrarlanabilir yöntem

Sürümleri karşılaştırmak için:

1. Kartı yeniden başlat, 1 dakika bekle (açılış işleri bitsin). Wi-Fi durumu iki ölçümde aynı olsun.
2. Infinity → Testler → **Tam test**. Raporu sakla.
3. Ayarlar → Gelişmiş → Varsayılanlara dön (Orta, Orta, Obsidyen, 60 sn, float). MAKS modda BAŞLA;
   süre dolunca rapor kendiliğinden kaydedilir. Sonra AŞIRI ile aynısı.
4. Karşılaştırılacak sayılar: işlemci testinde Mit/s ve hızlanma; ekran testinde Mpx/s; fraktal testinde
   tamamlanan kare, ortalama FPS ve ortalama Mit/s. Yol deterministik olduğu için aynı ayarlarla ilk N kare
   her sürümde aynı işi içerir; daha hızlı sürüm 60 sn'de daha çok kare tamamlar.
5. Her ölçümü en az üç kez yinele, ortancayı al (Wi-Fi ve Bluetooth çekirdek 0'da kesinti yapar).

## Derleme ayarı

Bütün yazılım Arduino-ESP32'nin varsayılanı `-Os` (boyut için en iyileme) ile derlenir; flash payı dar
olduğu için öyle kalır. Mandelbrot çekirdekleri (`bnRowF`, `bnRowQ`, `bnRowD`) işlev özniteliğiyle `-O2`
derlenir (`BN_HOT`). Derlenmiş kodda float iç döngüsü yalnızca FPU yazmaçlarında çalışır (iki nokta için
4 çarpma, 2 çarp-topla, 2 karşılaştırma, bellek erişimi yok).

## Derleme ve yükleme

Yeni bağımlılık yok. Her zamanki gibi: `pio run -e cyd` (ya da `-e cyd_st7789`), yükleme `pio run -e cyd
-t upload`; GitHub'daki sürümler kart tarafından kendiliğinden kurulur. Bilgisayarda sınama:
`firmware/test/host/run.sh test` ve ekran görüntüleri için `run.sh preview kiyas`.

## Bilinen sınırlar

- **Kartta denenmedi.** Derleme, bilgisayardaki birim testleri ve ekran önizlemeleri geçti; FPS, Mit/s,
  SPI verimi ve çift çekirdek hızlanması gibi sayılar ancak kartta ölçülünce bilinir.
- **DMA kullanılmıyor.** TFT_eSPI'nin ESP32 DMA yolu ESP-IDF SPI sürücüsünü (≈8 KB flash) getiriyor ve
  kapatılırken (`spi_bus_free`) ekran bacaklarını sıfırlıyor; açılınca kart yeniden başlayana dek bağlı
  kalması gerekiyor. Flash payı az, kazanç da sınırlı: aktarım kare süresinin küçük bir bölümü (raporda
  "Ekran aktarımı / kare"); en çok bu kadar kazanılabilir.
- Ekran yolu 40 MHz'te (55 MHz istense de); kuramsal tavan 2,5 Mpx/s.
- 320×240'ın tamamı için kare ara belleği yok (150 KB): bantlarla çizilir. Görüntü durunca bellekte
  tutulmaz; pencere kapanınca aynı kare yeniden hesaplanır.
- Hesap / kare değeri çekirdek 1'deki aktarım kesintilerini içerebilir (yukarıda).
- double yazılımla hesaplanır ve float'tan onlarca kat yavaştır; Aşırı yük'te derin kareler saniyeler sürer.
- Flash payı: bu uygulama ≈47 KB tutar; yazılımın boş flash'ı ≈21 KB'a indi.
- İlk kart ölçümü (fw-v72, ST7789, Aşırı yük, Orta): bellek tek işçiye yetmişti (çekirdek 0 boşta kaldı),
  2,79 FPS, 4,67 Mit/s, aktarım 25,6 ms/kare (2,2 Mpx/s, 40 MHz yolun %88'i). Bu yüzden başlarken
  bulut belleği bekleniyor ve iki işçili küçük düzenler eklendi.
- İkinci ölçüm (fw-v74, Aşırı yük, Pano): iki işçi (%52 / %47), 4,07 FPS, 6,51 Mit/s; ama yalnız 2 yuva
  vardı ve kabuk 16 ms'de bir gönderdiği için işçiler zamanın yarısında boş bekledi; pano çizimi de
  248 ms/sn tuttu (her harf SD'den). fw-v76: 4 ince yuva, 4 ms tur, glif önbelleği, seyrek ve yalnız
  değişen pano.
