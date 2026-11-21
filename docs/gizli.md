# DAXEN VANE · gizli özellikler

Beş gizli ekran var. Hepsi Lua ile yazıldı (`firmware/lua/gizli/`), varlık paketinden SD karta
(`/daxen/gizli/`) açılıp yerleşik uygulama olarak çalışır: izin sorulmaz, izinleri dosyanın kendi
satırındadır. Hiçbiri arka planda çalışmaz; çıkınca süreç kapanır. SD kart gerekir.

| Ad | Nasıl açılır | Ne yapar |
| --- | --- | --- |
| Project VANE | Ayarlar → Sistem'de logoya beş kez dokun | Kartın hikâyesi, yıldızlı arka planla |
| Developer Awakening | Geliştirici seviyesi ilk kez açılınca; sonra Terminal'de `awaken` (geliştirici seviyesi) | Sürüm, çip, bellek ve çalışma süresini akan satırlarla yazar |
| Ghost Protocol | Terminal'de `ghost` | Ekran kararır, ışık söner, soluk bir saat dolaşır; dokununca çıkar |
| Dark Radar | Ağ → Bluetooth sekmesinin adına uzun bas ya da Terminal'de `radar` | Yakındaki cihazların gerçek reklamları; merkeze yakınlık sinyal gücüdür, uzaklık değil. Hiçbir cihaza bağlanılmaz |
| The Unknown | Gece 03:00–03:59 arasında ana ekrandaki kediye dokun | Kartın o anki gerçek durumundan (saat, çalışma süresi, boş bellek, Wi-Fi sinyali) bozulan satırlar |

Terminal komutları `help` listesinde görünmez. Hiçbir ekran sahte veri göstermez: radar yalnızca
gerçek taramayı, bilgi satırları kartın gerçek durumunu yazar.
