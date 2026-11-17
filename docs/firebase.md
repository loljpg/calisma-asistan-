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
    events/{id}   Cihaz ekler (POST), telefon işleyip siler
    status        Cihaz yazar, telefon okur
```

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

Cihaz her işlem için `POST …/device/events.json` ile yeni kayıt ekler:

| type | Alanlar | Anlamı |
| --- | --- | --- |
| `complete` | `itemId`, `date`, `at` | Oturuma tik atıldı (planlanan süre kadar çalışma sayılır) |
| `uncomplete` | `itemId`, `date`, `at` | Tik geri alındı |
| `session` | `sec`, `date`, `at`, `itemId` (varsa), `ders`, `konu` | Kronometre bitti; 60 sn – 6 saat arası kaydedilir, oturum varsa tamamlanır |

`at`: milisaniye cinsinden zaman. Telefon aynı olayı iki kez uygulamaz.

### device/status

```json
{ "lastSeen": { ".sv": "timestamp" }, "fw": "v3" }
```

Cihaz bunu dakikada bir yazar. Telefon son 3 dakikada haber alırsa "Masa saati bağlı" gösterir.
