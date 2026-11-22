# DAXEN VANE · bellek bütçesi

ESP32'nin 320 KB'lık belleğinin bir kısmını ESP-IDF, Wi-Fi ve Bluetooth yığınları alır; kalanı
yazılımla paylaşılır. Bu belge kalanın nasıl bölündüğünü ve ağır işlerin hangi kuralla başladığını
anlatır. Kartta gerçek değerler Terminal'de `memory` ve `tasks`, Görevler uygulamasında ve
Ayarlar → Sistem'de görünür.

## Taban

Boş bellek hiçbir işle **18 KB**'ın altına inmez. Bu pay Wi-Fi'nin ve Bluetooth'un anlık
ayırmalarına, ekrana ve günlüğe kalır. Açılıştan beri en düşük değer `memory` komutunda tabanla
karşılaştırılır; her 1 KB düşüşte günlüğe yazılır (`/daxen/sys/gunluk.txt`, etiket `bellek`).

## Sürekli tutulanlar

| Ne | Boy | Not |
| --- | --- | --- |
| Durağan değişkenler (.data + .bss) | 67 KB | ESP-IDF, Wi-Fi, NimBLE ve yazılım birlikte |
| Çekirdek görevi (arayüz, servisler) | 8 KB yığın | Arduino döngü görevi |
| Ağ görevi | 16 KB yığın | Şifreli bağlantı el sıkışması burada |
| Ara bellek (büyük rakamlar, kedi) | 6,9 KB | 4 bit, 16 renkli palet; 16 bit olsaydı 27,5 KB |
| Yazı tiplerinin karakter dizinleri | yaklaşık 4 KB | İlk kullanımda; harf görüntüleri SD'den okunur |
| Günlük halkası | 2 KB | Son satırlar, SD yokken de |

Yazı tipleri, logo ve Lua dosyaları yazılıma gömülü değildir; SD karttaki varlık paketinden
(`/daxen/sys/varlik.bin`) okunur. Paket yoksa ya da eskiyse kart onu çalıştığı sürümün GitHub
sayfasından indirir. SD kart yoksa ekran yerleşik yazıyla çalışır (Türkçe harfler ç → c gibi
yazılır), Wi-Fi ayarlanabilir.

## Ağır işler

Kendi yığını ya da büyük tamponu olan işler başlamadan önce bellek bekçisine sorar
(`src/os/membudget.h`, karar `src/core/memcalc.h`):

| İş | Gereksinim | En büyük blok | Ne zaman |
| --- | --- | --- | --- |
| Lua uygulaması | 14 KB yığın + en az 16 KB Lua belleği | 14 KB | Uygulama açılırken |
| Web sunucusu (Aktarım) | 10 KB yığın + 4 KB tampon + 4 KB sunucu | 10 KB | Aktarım açılırken |
| Ağ aracı (ping, dns, tcp, traceroute, test) | 6 KB yığın | 6 KB | Komut verilince, iş bitince kapanır |
| Satranç motoru | 10 KB yığın | 10 KB | Yalnızca kart düşünürken |
| Bluetooth taraması | cihaz listesi | liste boyu | Tarama başlarken |
| Kıyas testi (Infinity) | 4 × 5 KB bant + 2 × 3 KB işçi yığını (yetmezse 4 × 2,5 KB, sonra 2 × 2,5 KB; iki işçi hep önce, tek işçi en son) + test sürerken 3,5 KB glif önbelleği | 5 KB | Görüntü hesaplanırken ve test sürerken; boştayken hepsi geri verilir. Başlarken bulut akışı bırakılır, en iyi düzen sığana dek en çok 3 sn beklenir |

Kurallar:

1. İş ancak **gereksinim + 18 KB taban + 2 KB pay** boşsa ve en büyük boş blok işin yığınını
   (+512 bayt) alabiliyorsa başlar.
2. Aynı anda en çok **3 ağır iş** çalışır. Dördüncüsü açık işlerin adıyla reddedilir; birini
   kapatıp yeniden denersin.
3. Reddedilen iş nedeni ekranda söyler ("Bellek yetersiz: 40 KB gerekiyor, 31 KB boş",
   "Aynı anda en çok 3 ağır iş çalışır; açık: …"). Aynı ret 30 saniyede bir kez günlüğe yazılır.
4. Satranç motoru sığmazsa hamleyi ayrı süreç yerine arayüz görevinde düşünür (ekran o sırada bekler).
5. Web sunucusu ya da ağ aracı belleğe sığmazsa bulutun canlı akışı iş boyunca duraklatılır
   (şifreli bağlantı on binlerce bayt tutar); iş bitince geri açılır.

## Kendiliğinden kapananlar

- **Web sunucusu:** yalnızca Aktarım (ya da Ayarlar → Bulut hesabı → *Tarayıcıdan gir*) açıkken
  çalışır, o ekrandan çıkınca kapanır. Üstünde başka bir ekran açık kalırsa 10 dakika istek
  gelmeyince kapanır (yükleme sürerken kapanmaz). Yığını, tamponu ve bağlantıları serbest kalır;
  bulut akışı geri gelir.
- **Uygulama listeleri:** Fotoğraflar, Dosyalar, Bilgi kartları, Çizim, Terminal ve Ağ kapanınca
  listelerini bellekten atar; açılınca yeniden okur.
- **Ağ aracı:** iş bitince süreç kapanır.
- **Satranç motoru:** hamle bulununca süreç kapanır.
- **Bluetooth taraması:** kullanılmayan cihaz listesi bırakılır.
- **Gizli özellikler** ([gizli.md](gizli.md)): hiçbiri arka planda çalışmaz; çıkınca kapanır.
- **Telefon Bluetooth'la bağlıyken** bulutun canlı akışı kapatılır; plan ve sayaç telefondan gelir.

## Yığın boyları

Görevlerin yığın boşluğu Görevler'de ve `tasks` komutunda görünür. Bir görevin boşluğu 1,5 KB'ın
altına inerse günlüğe bir kez yazılır (`yığın azaldı: … · açık: <uygulama>`). Yığın boyları bu
kayıtlara göre ayarlanır; ölçmeden küçültülmez (taşan yığın kartı çökertir).

Ölçümler (fw-v76, kart): çekirdek görevinde 608 B boş kalmıştı. Sebep: açılıştaki güncelleme
denetimi şifreli bağlantıyı çekirdek görevinin içinde açıyordu. fw-v78'den beri açılıştaki şifreli
işler (güncelleme denetimi, varlık paketi) kendi 12 KB yığınlı geçici görevinde çalışır ve
kullandıkları yığın günlüğe yazılır (`güncelleme denetimi: yığının N B'ı kullanıldı`). Ağ görevinin
16 KB'ı bulut bağlıyken ölçülünce küçültülecek (o ölçümde 13 KB boştu ama şifreli bağlantı açılmamıştı).
