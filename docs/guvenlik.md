# Güvenlik

Kartın korumaları ve sınırları. Kod: `core/auth.h`, `core/authcalc.h`, `os/authui.h`,
`sys/backup.h`, `sys/net.h` (TLS), `sys/web.h` (giriş kodu).

## PIN

Ayarlar → Güvenlik → PIN. 4–8 rakam, iki kez girilir. NVS'de yalnızca rastgele 16 baytlık tuz ve
SHA-256(tuz ‖ PIN) durur; PIN hiçbir günlüğe, ekrana ya da seri porta yazılmaz (giriş şifre
kipinde, rakam klavyesiyle). Değiştirmek ve kaldırmak şu anki PIN'i ister.

Yanlış denemeler: ilk dördü serbest, 5.'den sonra 30 sn, her yanlışta iki katı, en çok 15 dk.
Sayaç NVS'de durur; kartı yeniden başlatmak beklemeyi sıfırlamaz.

## Yetki seviyeleri

| Seviye | Nasıl | Neler |
| --- | --- | --- |
| Kullanıcı | Her zaman | Bütün uygulamalar; Terminal'in bilgi, ağ, Bluetooth ve `lua list/info/log` komutları, `backup` |
| Geliştirici | PIN'le (Ayarlar → Güvenlik ya da Terminal'de `dev`), 10 dk | Ek olarak `lua remove`, `lua stop`, `logs clear`, `restore` |

Geliştirici seviyesi 10 dk sonra, `dev off` ile ya da ekran kilitlenince kapanır. Seri porttan
`dev` yazılınca PIN kartın ekranında sorulur. Kritik işlemler (yeniden başlatma, silme, geri
yükleme, günlüğü silme) seviyeden bağımsız olarak kartta onay ister.

## Ekran kilidi

Ayarlar → Güvenlik → Ekran kilidi (PIN gerekir). Açılışta ve 5 dk dokunulmayınca ana ekran
kilitlenir: saat ve hava görünür, durum çubuğunda kilit simgesi çıkar, her dokunuş ve BOOT
önce PIN ister. Çalan alarm kilitliyken de ertelenip kapatılabilir.

Kilit yalnızca ekranı korur: telefonun Bluetooth bağlantısı ve USB seri port çalışmaya devam eder.

## Bulut (Firebase)

- **Sertifika doğrulaması:** Firebase'in giriş, oturum yenileme ve veritabanı bağlantıları
  Google'ın kökleriyle (GTS Root R1 ve R4, `certs/google-roots.pem`) doğrulanır; diğer HTTPS
  bağlantıları güncellemenin kök paketiyle. Doğrulanamazsa bağlantı kurulmaz. CI her derlemede
  Google'ın gerçek zincirlerini bu köklerle dener. Kartta tarih denetimi yoktur (mbedTLS'te
  saat kapalı); geçerlilik süresi dolmuş bir sertifika ad ve imza tutarsa kabul edilir.
- **Şifre saklanmaz:** şifre yalnızca ilk girişte kullanılır. Giriş başarılı olunca Firebase'in
  yenileme anahtarı saklanır, şifre NVS'den silinir. Anahtar geçersiz olursa (şifre değişti,
  hesap kapandı) kart "şifreyi yeniden gir" der. Veritabanı, API anahtarı ya da e-posta
  değişince eski anahtar silinir.

## Ayar yedeği

Ayarlar → Güvenlik → Ayarları yedekle: `/daxen/yedek/ayarlar.json`. Ekran, Pomodoro, ekran kilidi,
hava konumu, dünya saatleri, bulut hesabı (adres, e-posta) ve kayıtlı Wi-Fi adları. **Şifreli**
seçilirse (PIN varsa PIN ister) Wi-Fi şifreleri, Web API anahtarı ve bulut oturum anahtarı da
girer: bu dosyayı paylaşma. PIN, dokunmatik ayarı ve oyun kayıtları yedeğe girmez.

Geri yükleme PIN (varsa) ve onay ister; yalnızca yedekte olanlar değişir. Terminal: `backup`
(şifresiz), `restore` (geliştirici).

## Tarayıcıdan erişim

Aktarım açıkken kartın ekranında 6 haneli bir giriş kodu görünür; tarayıcı bu kodu ister.
5 yanlış kodda giriş 60 sn kilitlenir. Oturum çerezi HttpOnly ve SameSite=Strict. Bağlantı yerel ağda
düz HTTP'dir: aynı ağdaki biri trafiği görebilir, Aktarım'ı yalnızca güvendiğin ağda aç.

## Sınırlar

- **NVS şifrelenmemiş:** flash şifrelemesi kapalı. USB ile flash'ı okuyabilen biri NVS'yi de
  okur: Wi-Fi şifreleri ve bulut oturum anahtarı oradadır. PIN'in kendisi değil, özeti durur;
  4–8 rakamlık PIN'in özeti bilgisayarda denenerek bulunabilir, yani PIN fiziksel erişime karşı
  koruma değildir. Flash şifrelemesi bölüm tablosu ve önyükleyici değişikliği ister; açılmadı.
- **Güncellemeler** GitHub'dan sertifika doğrulamasıyla iner ve geri alma korumalıdır (yeni
  yazılım 45 sn içinde çalıştığını bildirmezse eskisi açılır); yazılımın kendisi imzalı değil.
- Seri porttaki Terminal fiziksel erişim sayılır; kullanıcı seviyesindeki komutlar kilitliyken
  de çalışır.
