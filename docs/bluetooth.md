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
