# Kendi uygulamanı yaz (Lua)

DAXEN VANE, SD karttaki `/daxen/uygulamalar/*.lua` dosyalarını **Uygulamalar** ekranında
ayrı birer uygulama olarak gösterir. Dosyayı Aktarım uygulamasıyla tarayıcıdan yükleyebilir
ya da kartı bilgisayara takıp kopyalayabilirsin. Uygulamalar ekranı her açıldığında klasör
yeniden okunur.

Klasör ilk kez oluşturulurken içine dört örnek (`merhaba.lua`, `zar.lua`, `sayac.lua`,
`top.lua`) ve bu belgenin kısa hali `REHBER.txt` yazılır. Örnekleri silebilirsin; yalnızca
klasörün kendisi silinirse örnekler bir sonraki açılışta yeniden yazılır.

Dil Lua 5.4'tür; tam sayılar ve ondalıklı sayılar 32 bittir. `string`, `table`, `math`,
`utf8` ve `coroutine` kitaplıkları açıktır. `io`, `os`, `require`, `dofile`, `loadfile`
kapalıdır; dosyalara yalnızca aşağıdaki `dosya` arabirimiyle erişilir. `print` seri porta yazar.

## Dosyanın başı

```lua
-- ad: Benim Uygulamam
-- simge: oyun
```

İlk altı satırdaki `-- ad:` ve `-- simge:` satırları okunur. `ad` yoksa dosya adı kullanılır.

Simge adları: `kod oyun zar saat alarm not liste hesap grafik muzik kalem kitap isik hedef
kronometre dunya bulut gunes ates bayrak kart yilan foto klavye cip`

## Olaylar

Uygulama, tanımladığın genel işlevlerle çalışır. Hepsi isteğe bağlıdır.

| İşlev | Ne zaman |
| --- | --- |
| `baslat()` | Uygulama açılınca bir kez |
| `ciz()` | Ekranın baştan çizilmesi gerektiğinde (açılış, bildirim balonu sonrası vb.) |
| `dokun(x, y)` | Ekrana dokunup bırakınca |
| `kaydir(yon)` | Kaydırma: `"sol"`, `"sag"`, `"yukari"`, `"asagi"` |
| `bas(x, y)`, `surukle(x, y)`, `birak(x, y)` | Parmağı adım adım izlemek için. Bunlardan biri tanımlıysa `dokun` ve `kaydir` gelmez |
| `adim(dt)` | Saniyede yaklaşık 30 kez; `dt` geçen milisaniye. Animasyon ve oyunlar için |
| `kapat()` | Uygulamadan çıkarken |

Sol üst köşedeki geri tuşu ve ekranın sol kenarından sağa kaydırma her zaman sisteme aittir.

## Ekran

Ekran 320 × 240 pikseldir. Üstteki 30 piksel başlık çubuğudur (`ekran.ust`); bu alana çizim yapılmaz.

| İşlev | Açıklama |
| --- | --- |
| `ekran.temizle([renk])` | Başlığın altını temizler |
| `ekran.dikdortgen(x, y, en, boy, renk, [dolu])` | `dolu` varsayılan `true` |
| `ekran.cizgi(x1, y1, x2, y2, renk, [kalinlik])` | Kalınlık 1'den büyükse yumuşak kenarlı çizgi |
| `ekran.daire(x, y, r, renk, [dolu])` | |
| `ekran.piksel(x, y, renk)` | |
| `ekran.yazi(metin, x, y, [renk], [boyut], [hiza], [arka])` | Boyut 1–5 (1 küçük, 2 normal, 3 büyük; 4 ve 5 yalnızca rakam ve işaretler). Hiza `"sol"`, `"orta"`, `"sag"`. Yazının genişliğini döndürür |
| `ekran.genislik(metin, [boyut])` | Yazının piksel genişliği |
| `ekran.renk(k, y, m)` | 0–255 değerlerden renk |
| `ekran.simge(ad, x, y, [boyut], [renk])` | Sistem simgelerinden biri |
| `ekran.en`, `ekran.boy`, `ekran.ust` | 320, 240, 30 |

Hazır renkler: `renk.zemin yuzey yuzey2 gumus gri soluk cizgi vurgu siyah beyaz kirmizi
turuncu sari yesil mavi mor`. `renk.vurgu` sistemin vurgu rengidir (kapak temasında değişir).

## Zaman

| İşlev | Döndürür |
| --- | --- |
| `zaman.ms()` | Açılıştan beri geçen milisaniye |
| `zaman.saat()` | saat, dakika, saniye (saat ayarlı değilse `nil`) |
| `zaman.tarih()` | yıl, ay, gün, haftanın günü (0 = pazar) |

## Dosyalar ve ayarlar

Her uygulamanın kendi klasörü vardır: `/daxen/uygulamalar/veri/<dosya adı>/`. Dosya adlarında
`/` ve `..` kullanılamaz; başka bir klasöre erişilemez.

| İşlev | Açıklama |
| --- | --- |
| `dosya.oku(ad)` | Metni döndürür (en çok 32 KB), yoksa `nil` |
| `dosya.yaz(ad, metin)` | Dosyayı baştan yazar |
| `dosya.ekle(ad, metin)` | Sonuna ekler |
| `dosya.var(ad)` | `true` / `false` |
| `dosya.sil(ad)` | |
| `dosya.liste()` | Dosya adlarından oluşan tablo |
| `ayar.al(ad, [varsayilan])` | Kayıtlı değeri metin olarak döndürür |
| `ayar.kaydet(ad, deger)` | `ayarlar.txt` içine yazar |

## Sistem

| İşlev | Açıklama |
| --- | --- |
| `sistem.bildir(metin)` | Alttaki kısa balon |
| `sistem.cikis()` | Uygulamayı kapatır |
| `sistem.isik()` | Ortam ışığı, yüzde |
| `sistem.led(k, y, m)` | Kartın RGB ışığı (kısa süre) |
| `sistem.bellek()` | Uygulamanın kalan belleği, bayt |

## Sınırlar

- Bellek: Bluetooth ve Wi-Fi açıkken boş belleğin bir kısmı, en çok 96 KB. Dolarsa `not enough memory` hatası görünür.
- Bir işlev 4 saniyeden uzun sürerse durdurulur (sonsuz döngüye karşı).
- Hata olursa ekranda dosya adı ve satır numarasıyla görünür; dosyayı düzeltip uygulamayı yeniden aç.

## Örnek

```lua
-- ad: Merhaba
-- simge: kod
local sayi = 0

function ciz()
  ekran.temizle()
  ekran.yazi("Merhaba DAXEN!", ekran.en / 2, 64, renk.gumus, 3, "orta")
  ekran.yazi(tostring(sayi), ekran.en / 2, 130, renk.vurgu, 5, "orta")
end

function dokun(x, y)
  sayi = sayi + 1
  ciz()
end
```
