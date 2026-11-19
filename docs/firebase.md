# Firebase veri yapısı

Telefon uygulaması ve masa saati (ESP32 CYD) aynı Firebase hesabıyla Realtime Database üzerinden konuşur. İkisi de REST uç noktalarını kullanır, SDK yoktur.

## Kurulum

1. console.firebase.google.com → yeni proje.
2. Authentication → Sign-in method → **E-posta/Şifre** açık.
3. Realtime Database → konum **europe-west1**, kilitli modda başlat.
4. Kurallar:

```json
{
  "rules": {
    "users": {
      "$uid": {
        ".read": "auth != null && auth.uid === $uid",
        ".write": "auth != null && auth.uid === $uid"
      }
    }
  }
}
```

Uygulamada: Ayarlar → Bulut ve masa saati → veritabanı adresi, Web API anahtarı, e-posta, şifre.

## Giriş

- Giriş: `POST https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=API_KEY` → `idToken`, `refreshToken`, `localId` (uid)
- Yenileme: `POST https://securetoken.googleapis.com/v1/token?key=API_KEY` (form: `grant_type=refresh_token&refresh_token=…`)
- Veritabanı: `https://DB_ADRESI/users/{uid}/…json?auth=ID_TOKEN`

## Ağaç

```
users/{uid}/
  backup          { savedAt, data }        Uygulamanın tüm verisi (API anahtarı hariç). Sadece telefon yazar.
  device/
    view          Telefon yazar, cihaz okur
    events/{id}   Cihaz ekler (PATCH), telefon işleyip siler
    status        Cihaz yazar, telefon okur
    timer         Ortak kronometre: ikisi de yazar, ikisi de okur
```

İki taraf da `users/{uid}/device.json` adresini **canlı akışla** (REST streaming, `Accept: text/event-stream`)
dinler; değişiklikler saniyesinde gelir. Akış koparsa telefon 30 sn'de bir, cihaz plan için 30 sn'de,
kronometre için 10 sn'de bir sorar.

### device/view

```json
{
  "date": "2026-11-17",
  "items": [
    { "id": "m3x…", "saat": "16:00", "ders": "Fizik", "konu": "Kuvvet ve hareket", "dk": 30, "durum": "bekliyor" }
  ],
  "studiedMin": 45,
  "goalMin": 300,
  "streak": 6,
  "examDays": 210,
  "updatedAt": 1794950000000
}
```

`durum`: `bekliyor` | `tamam` | `ertelendi`. `examDays` sınav tarihi girilmemişse `null`.

### device/events

Cihaz olayları `PATCH …/device/events.json` ile 20'şerli paketlerle gönderir. Anahtar olayın
içeriğinden üretilir (`c` + 16 hane), böylece yeniden denenen paket çift kayıt oluşturmaz.
İnternet yokken olaylar SD kartta (`/calisma/kuyruk.txt`), kart yoksa cihazın hafızasında (en fazla 40) bekler.

| type | Alanlar | Anlamı |
| --- | --- | --- |
| `complete` | `itemId`, `date`, `at` | Oturuma tik atıldı (planlanan süre kadar çalışma sayılır) |
| `uncomplete` | `itemId`, `date`, `at` | Tik geri alındı |
| `postpone` | `itemId`, `date`, `at` | Oturum ertelendi (tamamlanmışsa yok sayılır) |
| `session` | `sec`, `date`, `at`, `itemId` (varsa), `ders`, `konu` | Kronometre bitti; 60 sn – 6 saat arası kaydedilir, oturum varsa tamamlanır |

`at`: milisaniye cinsinden zaman. Telefon aynı olayı iki kez uygulamaz.

### device/status

```json
{ "lastSeen": { ".sv": "timestamp" }, "fw": "fw-5", "variant": "cyd_st7789", "ip": "…", "rssi": -60,
  "heap": 120000, "sd": true, "live": true, "queue": 0 }
```

Cihaz bunu dakikada bir yazar. Telefon son 3 dakikada haber alırsa "Masa saati bağlı" gösterir.

### device/timer

```json
{ "on": true, "itemId": "m3x…", "date": "2026-11-19", "ders": "Fizik", "konu": "Kuvvet", "dk": 45,
  "startedAt": 1795580000000, "acc": 120000, "by": "device", "rev": 1795580000000 }
```

- `startedAt`: çalışıyorsa son başlatılma anı (ms), duraklatıldıysa `0`. Geçen süre = `acc + (şimdi − startedAt)`.
- `on: false`: kronometre kapalı. Bitiren taraf oturumu kendisi kaydeder (cihaz `session` olayıyla);
  diğer taraf yalnızca kronometresini kapatır, böylece süre iki kez sayılmaz.
- `rev`: değişiklik zamanı. Her taraf yalnızca gördüğü en yeni `rev`'den büyük olanı uygular;
  kendi yazdığının yankısını böylece yok sayar.
