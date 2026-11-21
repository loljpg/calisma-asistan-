# Terminal

DAXEN VANE'in kendi komut yorumlayıcısı. Bir Linux kabuğu değildir: yalnızca aşağıdaki tablodaki
komutlar çalışır (izin listesi `apps/terminal.h` içindeki `TL_CMDS`), hepsi gerçek değer okur ve
girdi asla fonksiyon adı, bellek adresi ya da serbest dosya yolu olarak kullanılmaz
(`core/cmd.h`).

Ekrandan: Uygulamalar → **Terminal**. Alttaki kısayollar (`help`, `sysinfo`, `memory`, `tasks`,
`logs`) tek dokunuşla çalışır; "komut yaz" klavyeyi açar, "son" önceki komutları sırayla getirir.
Yukarı/aşağı kaydırarak eski çıktı okunur.

USB seri porttan: 115200 baud, her komut bir satır (`\n`). Çıktı aynı porta yazılır.

| Komut | Ne yapar |
| --- | --- |
| `help` | Komutları listeler |
| `clear` | Ekranı temizler |
| `history` | Son 16 komut |
| `sysinfo` | Sürüm, çip, çalışma süresi, son açılış, varsa önceki çökme, bellek, SD, Wi-Fi, Bluetooth, çekirdek yükü |
| `version` | Yazılım sürümü ve modeli, çip, Lua sürümü |
| `uptime` | Açılıştan beri geçen süre |
| `memory` | Boş bellek, açılıştan beri en az (18 KB tabanıyla karşılaştırılır), en büyük blok |
| `tasks` | Servisler ve görevler: durum, işlemci payı (ölçülmüyorsa `—`), yığın boşluğu |
| `apps` | Yerleşik ve Lua uygulamaları |
| `app launch <ad>` | Uygulamayı açar (ad başlatıcıdaki gibi; boşluk varsa tırnak içinde) |
| `storage` | SD kart dolu/boş, yazılım yuvası, günlük dosyası |
| `logs [satır]` | Sistem günlüğünün son satırları (varsayılan 20) |
| `wifi status` | Bağlı ağ, adres, sinyal |
| `bluetooth status` | Telefon bağlantısı |
| `reboot` | Kartta onay penceresi açar, onaylanırsa yeniden başlatır |

Sınırlar: satır en fazla 96 karakter, en fazla 6 argüman, çift tırnak içi tek argüman. Çıktı ve
geçmiş yalnızca Terminal açıkken bellekte tutulur (en fazla 120 satır).

Sistem günlüğü (`logs`) açılışı, önceki çökmenin özetini (çekirdek dökümünden), Wi-Fi ve telefon
bağlantılarını, güncellemeleri, web sunucusunu ve en düşük boş belleğin her 1 KB düşüşünü yazar.
RAM'de son 2 KB tutulur; SD varsa `/daxen/sys/gunluk.txt` dosyasına da eklenir. Şifre, PIN ya da
giriş kodu günlüğe yazılmaz.

Sonraki aşamalar: ağ araçları (`wifi scan`, `wifi ping`, `wifi dns`, `wifi tcp`, `wifi traceroute`),
`bluetooth scan`, geliştirici komutları (PIN ile).
