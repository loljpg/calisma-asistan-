# Bluetooth bağlantısı

Masa saati (çevre birimi) ile telefon (merkez) Bluetooth Low Energy ile konuşur. Wi-Fi gerekmez.
Telefonda bağlantıyı Android servisi tutar (`app/plugins/masa-saati`); uygulama kapalıyken de çalışır.

## Servis

| | UUID | Yön |
| --- | --- | --- |
| Servis | `6e3a0001-5a17-4c2d-9b8e-0c41a5a7c0de` | |
| RX | `6e3a0002-…` | telefon yazar (yanıtlı yazma) |
| TX | `6e3a0003-…` | masa saati bildirir (notify) |

Cihaz adı `CalismaSaati`. MTU 247 istenir.

## Çerçeve

`0xA5` · tür (1 bayt, ASCII) · uzunluk (2 bayt, küçük uçlu) · veri. Çerçeveler BLE paketlerine
bölünür, karşı taraf akışı birleştirir. Veri JSON'dur; `I` ve `A` görüntüdür:
kimlik (4 bayt) · genişlik (2) · yükseklik (2) · küçük uçlu RGB565 pikseller. Simgelerde `0xF81F` saydamdır.

## Eşleşme

Telefon bağlanınca `H` yollar: `{ "token": "32 hane", "pair": bool, "t": ms }`. Masa saati
kayıtlı anahtarla aynıysa ya da hiç eşleşmemişse veya eşleşme kipindeyse (Ayarlar → Bluetooth, 2 dk)
kabul eder ve `h` ile yanıtlar: `{ "ok": true, "paired": bool, "fw": "fw-6", "variant": "…" }`.
Kabul etmezse bağlantıyı keser. Telefon "Eşleşmeyi kaldır" deyince `U` yollar, masa saati anahtarı siler.

## Mesajlar

Telefon → masa saati

| Tür | Veri | Anlamı |
| --- | --- | --- |
| `C` | `{ t }` | Saat (internet yokken de doğru saat) |
| `V` | device/view ile aynı | Günün planı |
| `T` | device/timer ile aynı | Ortak kronometre |
| `a` | `{ n }` | Son olay paketi alındı; masa saati kuyruktan siler |
| `N` | `{ id, app, pkg, title, text, ic, t }` | Bildirim |
| `X` | `{ id }` | Bildirim telefonda kapatıldı |
| `I` | görüntü 20×20 | Uygulama simgesi (`ic` kimliği) |
| `M` | `{ on, enabled, app, title, artist, playing, pos, dur, vol, volMax, art, accent, dark }` | Çalan müzik; `accent` ve `dark` kapaktan çıkarılan renkler `[r,g,b]` |
| `A` | görüntü 80×80 | Şarkı kapağı (`art` kimliği) |
| `U` | `{}` | Eşleşme kaldırıldı |

Masa saati → telefon

| Tür | Veri | Anlamı |
| --- | --- | --- |
| `h` | yukarıda | Eşleşme yanıtı |
| `E` | `{ anahtar: olay, … }` | En fazla 10 olay (device/events ile aynı); telefon `a` ile onaylar |
| `T` | device/timer ile aynı | Kronometre masa saatinde değişti |
| `K` | `{ cmd }` | Müzik komutu: `toggle`, `next`, `prev`, `volup`, `voldown` |
| `S` | `{ fw, variant, heap, sd, queue, wifi }` | Durum (dakikada bir) |

## Bulutla birlikte

Bluetooth bağlıyken masa saati bulut akışını kapatır, olayları ve kronometreyi telefona verir.
Telefon uygulaması bunları işler ve buluta yazar. Bağlantı koparsa masa saati Wi-Fi varsa buluta döner.

## Yakındaki cihazlar (tarama)

Kart telefon için çevre birimidir; Ağ uygulamasının Bluetooth sekmesi ve Terminal'deki
`bluetooth scan` için aynı anda gözlemci rolünde de çalışır. Merkezi rol kapalıdır: kart hiçbir
cihaza bağlanmaz, servis keşfi yapmaz, hiçbir şey yazmaz. Yalnızca cihazların herkese açık reklam
paketleri okunur.

- **Tarama:** etkin tarama, 100 ms aralıkla; telefon bağlıyken 20 ms, değilse 50 ms dinlenir ki
  telefonun bağlantı olaylarına yer kalsın. Çoğu cihaz adını tarama yanıtında verdiği için kart,
  her BLE taramasında olduğu gibi tarama isteği gönderir. Süre 10 sn (Terminal'de 3–30 sn).
- **Liste:** en fazla 30 cihaz. Doluysa yeni bir cihaz yalnızca listedeki en zayıftan güçlüyse
  onun yerine geçer; alınamayanlar sayılır. Liste tarama ve ekran süresince bellekte durur
  (yaklaşık 1,7 KB), Terminal'den yapılan taramanın listesi 5 dk sonra bırakılır.
- **Gösterilenler:** ad (tam ya da kısaltılmış), adres ve türü (genel, rastgele sabit, değişen
  gizlilik adresi), anlık ve en güçlü sinyal, üretici (Bluetooth SIG numarası; bilinen adlar
  listede), 16 bit servisler ve 128 bit servis sayısı, görünüm, yayın gücü, bağlanabilirlik,
  paket sayısı, ilk ve son görülme. Aynı servis kimliğini (6e3a0001-…) yayınlayan başka bir DAXEN
  kartı ayrıca belirtilir.
- **Gösterilmeyenler:** uzaklık (sinyal gücünden güvenilir uzaklık çıkmaz), cihazın gerçek kimliği
  (telefonların çoğu birkaç dakikada değişen gizlilik adresi kullanır).

Tarama NimBLE'nin kendi işleviyle yapılır; NimBLE-Arduino'nun tarama sınıfı kullanılmaz ve
derlemeye girmez (`platformio.ini`'deki `--wrap` açıklaması `sys/blescan.h`'de).
