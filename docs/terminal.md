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
| `wifi status` | Bağlı ağ, sinyal (dBm, yüzde), BSSID, kanal, adres ve alt ağ maskesi, ağ geçidi, DNS sunucuları, kartın MAC adresi |
| `wifi ip` | Adres, ağ geçidi ve DNS |
| `wifi saved` | Kayıtlı ağlar (şifreler gösterilmez) |
| `wifi scan` | Çevredeki erişim noktaları: her BSSID ayrı, sinyal, kanal, güvenlik türü (pasif tarama, bağlantı kesilmez) |
| `wifi ping <adres> [adet]` | ICMP yankı (1–10, varsayılan 4): her yanıtın süresi, kayıp yüzdesi, en az/ort/en çok |
| `wifi dns <ad>` | DNS sorgusu: adresler ve süre |
| `wifi tcp <adres> <port>` | TCP bağlantı testi (4 sn zaman aşımı) |
| `wifi traceroute <adres>` | Artan TTL'li ICMP ile yoldaki yönlendiriciler (en fazla 20 adım; yanıt vermeyen adım `*`) |
| `wifi test` | Ağ geçidi (ping), DNS ve internet (TCP 443) testi |
| `stop` | Süren ağ işini ya da Bluetooth taramasını durdurur |
| `bluetooth status` | Telefon bağlantısı (ve MTU), kartın Bluetooth adresi, reklamın açık olup olmadığı, son tarama |
| `bluetooth scan [sn]` | Yakındaki BLE cihazları (3–30 sn, varsayılan 10). Bitince liste yazılır |
| `bluetooth devices` | Son taramanın listesi: sinyal, adres, ad, üretici (en güçlü sinyal önce) |
| `bluetooth info <no>` | Listedeki bir cihazın reklamdaki her şeyi: adres türü, sinyal, üretici, servisler, yayın gücü, bağlanabilirlik, paket sayısı |
| `reboot` | Kartta onay penceresi açar, onaylanırsa yeniden başlatır |

Sınırlar: satır en fazla 96 karakter, en fazla 6 argüman, çift tırnak içi tek argüman. Çıktı ve
geçmiş yalnızca Terminal açıkken bellekte tutulur (en fazla 120 satır).

Sistem günlüğü (`logs`) açılışı, önceki çökmenin özetini (çekirdek dökümünden), Wi-Fi ve telefon
bağlantılarını, güncellemeleri, web sunucusunu ve en düşük boş belleğin her 1 KB düşüşünü yazar.
RAM'de son 2 KB tutulur; SD varsa `/daxen/sys/gunluk.txt` dosyasına da eklenir. Şifre, PIN ya da
giriş kodu günlüğe yazılmaz.

Ağ araçları ayrı bir süreçte (`ağ-araç`, 6 KB yığın) çalışır, çıktı geldikçe yazılır ve iş bitince
süreç kapanır; `wifi scan` süreç açmaz, ağ görevine yaptırılır. Boş bellek 18 KB tabanını koruyacak
kadar değilse bulutun canlı akışı geçici olarak duraklatılır, bellek açılınca araç başlar (en fazla
8 sn beklenir, açılmazsa nedeni yazılır) ve iş bitince akış geri gelir.
Açılıştan sonraki ilk kullanımda kartın ekranında onay istenir: bu araçlar yalnızca kullanma iznin olan
ağlarda teşhis içindir; tarama pasiftir, ağa müdahale eden hiçbir şey (parola deneme, trafik bozma) yoktur.

Bluetooth taraması yalnızca cihazların herkese açık yayınladığı reklam paketlerini okur; hiçbir
cihaza bağlanılmaz, hiçbir şey yazılmaz. Uzaklık gösterilmez (sinyal gücünden güvenilir uzaklık
çıkmaz). Ayrıntılar: [bluetooth.md](bluetooth.md#yakındaki-cihazlar-tarama).

Sonraki aşama: geliştirici komutları (PIN ile).
