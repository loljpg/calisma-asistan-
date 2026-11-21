#pragma once
// Bluetooth keşfinin donanımdan bağımsız kısmı (bilgisayarda test edilir): reklam paketinin (AD)
// çözülmesi, cihaz listesi, üretici / servis / görünüm adları ve ekranda gösterilen satırlar.
// Hepsi cihazların herkese açık yayınladığı reklam paketlerinden okunur; hiçbir cihaza bağlanılmaz.
// Uzaklık gösterilmez: sinyal gücünden güvenilir uzaklık çıkmaz.
// Üretici ve servis numaraları Bluetooth SIG listesinden (Nordic'in bluetooth-numbers-database'i
// ile karşılaştırıldı); listede olmayan numara olduğu gibi (0x…) gösterilir.
#include <stdarg.h>
#include <vector>

enum : uint8_t { BD_CONN = 1, BD_NOCONN = 2, BD_DAXEN = 4 };
static const int8_t BD_NO_TX = 127;
static const int BLE_LIST_CAP = 30;
#define BLE_ONLY_ADS "Yalnızca yayınlar okunur, hiçbir cihaza bağlanılmaz."

struct BleDev {
  uint8_t addr[6];   // ekrandaki sırayla (en anlamlı bayt önce)
  uint8_t addrType;  // 0 genel, 1 rastgele
  int8_t rssi, rssiMax, tx;
  uint8_t flags, nSvc, n128;
  uint16_t mfr;      // 0xFFFF: reklamda yok
  uint16_t appear;   // 0: yok
  uint16_t svc[3];   // 16 bit servis kimlikleri (en fazla 3)
  uint16_t seen;     // alınan paket
  char name[21];     // UTF-8, en fazla 20 bayt
  uint32_t first, last;
};

static void bleDevInit(BleDev& d) {
  memset(&d, 0, sizeof d);
  d.rssi = d.rssiMax = -127;
  d.tx = BD_NO_TX;
  d.mfr = 0xFFFF;
}

static void bleAddSvc(BleDev& d, uint16_t u) {
  for (int i = 0; i < d.nSvc; i++)
    if (d.svc[i] == u) return;
  if (d.nSvc < 3) d.svc[d.nSvc++] = u;
}

// "6e3a0001-…" biçimindeki kimlik, reklamdaki küçük uçlu 16 baytla aynı mı
static bool bleUuidIs(const uint8_t* le, const char* s) {
  uint8_t b[16] = {0};
  int n = 0;
  for (const char* p = s; *p && n < 32; p++) {
    char c = *p;
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (v < 0) continue;
    b[n / 2] |= (n & 1) ? v : v << 4;
    n++;
  }
  if (n != 32) return false;
  for (int i = 0; i < 16; i++)
    if (le[i] != b[15 - i]) return false;
  return true;
}

// Ad: geçerli UTF-8, en fazla 20 bayt, karakter ortasından kesilmez, denetim karakterleri atılır
static void bleSetName(BleDev& d, const uint8_t* p, size_t n) {
  size_t o = 0;
  for (size_t i = 0; i < n;) {
    uint8_t c = p[i];
    size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!len || i + len > n) break;
    bool ok = true;
    for (size_t k = 1; k < len; k++)
      if ((p[i + k] & 0xC0) != 0x80) ok = false;
    if (!ok || o + len > 20) break;
    if (len > 1 || c >= 0x20) {
      memcpy(d.name + o, p + i, len);
      o += len;
    }
    i += len;
  }
  d.name[o] = 0;
}

// Reklam ya da tarama yanıtı verisi: [uzunluk][tür][veri]… Bozuk bir alanda durur.
static void bleParseAd(const uint8_t* p, size_t n, BleDev& d) {
  size_t i = 0;
  while (i + 1 < n) {
    uint8_t len = p[i];
    if (!len || i + 1 + len > n) break;
    uint8_t type = p[i + 1];
    const uint8_t* v = p + i + 2;
    size_t vl = len - 1;
    switch (type) {
      case 0x02:
      case 0x03:  // 16 bit servis kimlikleri
        for (size_t k = 0; k + 1 < vl; k += 2) bleAddSvc(d, v[k] | v[k + 1] << 8);
        break;
      case 0x06:
      case 0x07:  // 128 bit servis kimlikleri
        for (size_t k = 0; k + 15 < vl; k += 16) {
          if (bleUuidIs(v + k, BLE_SVC)) d.flags |= BD_DAXEN;
          if (d.n128 < 255) d.n128++;
        }
        break;
      case 0x08:  // kısaltılmış ad (tam ad gelmediyse)
        if (!d.name[0]) bleSetName(d, v, vl);
        break;
      case 0x09:
        bleSetName(d, v, vl);
        break;
      case 0x0A:
        if (vl) d.tx = (int8_t)v[0];
        break;
      case 0x16:  // 16 bit servis verisi (Eddystone, Fast Pair…)
        if (vl >= 2) bleAddSvc(d, v[0] | v[1] << 8);
        break;
      case 0x19:
        if (vl >= 2) d.appear = v[0] | v[1] << 8;
        break;
      case 0xFF:  // üreticiye özel veri: ilk iki bayt üretici numarası
        if (vl >= 2) d.mfr = v[0] | v[1] << 8;
        break;
    }
    i += 1 + len;
  }
}

// Paketi listeye işler. Liste doluysa en zayıf cihazın yerine yalnızca ondan güçlü bir cihaz geçer;
// sığmayanlar dropped'a sayılır. Dönüş: listedeki yeri, -1 alınmadı.
static int bleMerge(BleDev* list, int& n, int cap, const BleDev& d, uint32_t now, uint16_t& dropped) {
  int at = -1;
  for (int i = 0; i < n && at < 0; i++)
    if (list[i].addrType == d.addrType && !memcmp(list[i].addr, d.addr, 6)) at = i;
  if (at < 0) {
    if (n < cap) {
      at = n++;
    } else {
      int w = 0;
      for (int i = 1; i < n; i++)
        if (list[i].rssiMax < list[w].rssiMax) w = i;
      if (dropped < 0xFFFF) dropped++;
      if (list[w].rssiMax >= d.rssi) return -1;
      at = w;
    }
    list[at] = d;
    list[at].rssiMax = d.rssi;
    list[at].first = list[at].last = now;
    list[at].seen = 1;
    return at;
  }
  BleDev& e = list[at];
  e.rssi = d.rssi;
  if (d.rssi > e.rssiMax) e.rssiMax = d.rssi;
  e.last = now;
  if (e.seen < 0xFFFF) e.seen++;
  if (d.name[0] && strlen(d.name) >= strlen(e.name)) memcpy(e.name, d.name, sizeof e.name);
  if (d.mfr != 0xFFFF) e.mfr = d.mfr;
  if (d.appear) e.appear = d.appear;
  if (d.tx != BD_NO_TX) e.tx = d.tx;
  for (int k = 0; k < d.nSvc; k++) bleAddSvc(e, d.svc[k]);
  if (d.n128 > e.n128) e.n128 = d.n128;
  e.flags |= d.flags & BD_DAXEN;
  if (d.flags & (BD_CONN | BD_NOCONN)) e.flags = (e.flags & ~(BD_CONN | BD_NOCONN)) | (d.flags & (BD_CONN | BD_NOCONN));
  return at;
}

// Gösterim sırası: en güçlü görülen sinyal önce (canlı listede satırlar az zıplasın), eşitse adres
static void bleOrder(const BleDev* l, int n, uint8_t* idx) {
  for (int i = 0; i < n; i++) {
    int j = i;
    while (j > 0) {
      const BleDev &a = l[idx[j - 1]], &b = l[i];
      if (a.rssiMax > b.rssiMax || (a.rssiMax == b.rssiMax && memcmp(a.addr, b.addr, 6) <= 0)) break;
      idx[j] = idx[j - 1];
      j--;
    }
    idx[j] = i;
  }
}

// Biçimli tek satır ekler (String birleştirmeden çok daha az kod tutar)
static void blePush(std::vector<String>& o, const char* fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  o.push_back(b);
}

static const char* bleCompany(uint16_t id) {
  static const struct {
    uint16_t id;
    const char* n;
  } T[] = {{0x0000, "Ericsson"},     {0x0001, "Nokia"},        {0x0002, "Intel"},          {0x0006, "Microsoft"},
           {0x0008, "Motorola"},     {0x000A, "Qualcomm"},     {0x000D, "Texas Instruments"}, {0x000F, "Broadcom"},
           {0x001D, "Qualcomm"},     {0x0046, "MediaTek"},     {0x004C, "Apple"},          {0x0057, "Harman"},
           {0x0059, "Nordic"},       {0x005D, "Realtek"},      {0x0075, "Samsung"},        {0x0087, "Garmin"},
           {0x009E, "Bose"},         {0x00C4, "LG"},           {0x00CC, "Beats"},          {0x00E0, "Google"},
           {0x012D, "Sony"},         {0x0131, "Cypress"},      {0x0157, "Huami"},          {0x0171, "Amazon"},
           {0x01AB, "Meta"},         {0x01DA, "Logitech"},     {0x01DD, "Philips"},        {0x027D, "Huawei"},
           {0x02C5, "Lenovo"},       {0x02E5, "Espressif"},    {0x02FF, "Silicon Labs"},   {0x038F, "Xiaomi"},
           {0x03FF, "Withings"},     {0x0499, "Ruuvi"},        {0x067C, "Tile"},           {0x072F, "OnePlus"},
           {0x079A, "Oppo"},         {0x07D0, "Tuya"},         {0x0822, "Adafruit"},       {0x0837, "vivo"},
           {0x08A4, "Realme"},       {0x09C6, "Honor"},        {0x0CC2, "Anker"},          {0x0CCB, "Nothing"}};
  for (auto& t : T)
    if (t.id == id) return t.n;
  return nullptr;
}

static const char* bleSvcName(uint16_t u) {
  static const struct {
    uint16_t u;
    const char* n;
  } T[] = {{0x1809, "Termometre"}, {0x180A, "Cihaz bilgisi"}, {0x180D, "Nabız"},       {0x180F, "Pil"},
           {0x1810, "Tansiyon"},   {0x1812, "Klavye/fare"},   {0x181A, "Ortam ölçümü"}, {0x181D, "Tartı"},
           {0x1826, "Spor aleti"}, {0x184E, "LE Audio"},      {0xFD6F, "Temas bildirimi"}, {0xFE2C, "Fast Pair"},
           {0xFEAA, "Eddystone"}};
  for (auto& t : T)
    if (t.u == u) return t.n;
  return nullptr;
}

// Görünüm (appearance) kategorisinin adı: değerin üst 10 biti (Bluetooth SIG). Yalnız sık görülen
// kategoriler; diğerleri ayrıntıda kodla gösterilir. Biçim: [kategori][ad]\0…
static const char* bleAppear(uint16_t a) {
  static const char N[] = "\x01" "Telefon\0\x02" "Bilgisayar\0\x03" "Kol saati\0\x05" "Ekran\0\x0F" "Klavye/fare\0\x21" "Hoparlör\0"
                          "\x25" "Kulaklık\0\x29" "İşitme cihazı\0";
  int cat = a >> 6;
  for (const char* p = N; *p; p += strlen(p) + 1)
    if (*p == cat) return p + 1;
  return nullptr;
}

static const char* bleAddrKind(const BleDev& d) {
  if (!d.addrType) return "genel adres";
  switch (d.addr[0] >> 6) {
    case 3: return "rastgele, sabit";
    case 1: return "gizlilik adresi (değişir)";
    case 0: return "rastgele, çözülemez";
    default: return "rastgele";
  }
}

static const char* bleTitle(const BleDev& d) { return d.name[0] ? d.name : d.flags & BD_DAXEN ? "DAXEN VANE" : "(adsız)"; }

// Listede ikinci satır: üretici · görünüm · adres (üretici ve görünüm yoksa bilinen ilk servis)
static String bleSub(const BleDev& d) {
  char b[96];
  int L = 0;
  const char* c = d.mfr != 0xFFFF ? bleCompany(d.mfr) : nullptr;
  const char* ap = d.appear ? bleAppear(d.appear) : nullptr;
  if (c) L = snprintf(b, sizeof b, "%s · ", c);
  else if (d.mfr != 0xFFFF) L = snprintf(b, sizeof b, "üretici 0x%04X · ", d.mfr);
  if (ap) L += snprintf(b + L, sizeof b - L, "%s · ", ap);
  const char* sv = !L && d.nSvc ? bleSvcName(d.svc[0]) : nullptr;
  if (sv) L = snprintf(b, sizeof b, "%s · ", sv);
  snprintf(b + L, sizeof b - L, "%s", macText(d.addr).c_str());
  return b;
}

// Terminal satırı
static String bleRow(int no, const BleDev& d) {
  char b[120];
  int L = snprintf(b, sizeof b, "%d. %d dBm  %s  %s", no, d.rssi, macText(d.addr).c_str(), bleTitle(d));
  const char* c = bleCompany(d.mfr);
  if (c) snprintf(b + L, sizeof b - L, " · %s", c);
  else if (d.mfr != 0xFFFF) snprintf(b + L, sizeof b - L, " · 0x%04X", d.mfr);
  return b;
}

static void bleAgo(uint32_t ms, char* b) {
  if (ms < 1000) strcpy(b, "şimdi");
  else snprintf(b, 24, "%u sn önce", (unsigned)(ms / 1000));
}

static void bleDetail(const BleDev& d, uint32_t now, std::vector<String>& o) {
  blePush(o, "Ad: %s", d.name[0] ? d.name : "reklamda yok");
  blePush(o, "Adres: %s · %s", macText(d.addr).c_str(), bleAddrKind(d));
  blePush(o, "Sinyal: %d dBm (%s) · en güçlü %d dBm", d.rssi, rssiWord(d.rssi), d.rssiMax);
  const char* c = bleCompany(d.mfr);
  if (c) blePush(o, "Üretici: %s (0x%04X)", c, d.mfr);
  else if (d.mfr != 0xFFFF) blePush(o, "Üretici: 0x%04X (listede yok)", d.mfr);
  else blePush(o, "Üretici: reklamda yok");
  if (d.appear) {
    const char* ap = bleAppear(d.appear);
    blePush(o, "Görünüm: %s (0x%04X)", ap ? ap : "listede yok", d.appear);
  }
  char sv[128];
  int L = 0;
  for (int k = 0; k < d.nSvc; k++) {
    const char* n = bleSvcName(d.svc[k]);
    L += snprintf(sv + L, sizeof sv - L, "%s%s%s0x%04X", k ? ", " : "", n ? n : "", n ? " " : "", d.svc[k]);
  }
  if (d.n128) L += snprintf(sv + L, sizeof sv - L, "%s%d adet 128 bit", L ? ", " : "", d.n128);
  blePush(o, "Servisler: %s", L ? sv : "reklamda yok");
  if (d.tx != BD_NO_TX) blePush(o, "Yayın gücü: %d dBm", d.tx);
  blePush(o, "Bağlanabilir: %s", d.flags & BD_CONN ? "evet" : d.flags & BD_NOCONN ? "hayır" : "bilinmiyor");
  char a1[24], a2[24];
  bleAgo(now - d.first, a1);
  bleAgo(now - d.last, a2);
  blePush(o, "Paket: %u · ilk %s · son %s", (unsigned)d.seen, a1, a2);
  if (d.flags & BD_DAXEN) blePush(o, "Başka bir DAXEN VANE kartı (aynı servis kimliği)");
}
