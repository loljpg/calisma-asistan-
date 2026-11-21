#pragma once
// Ağ teşhisinin donanımdan bağımsız hesapları (bilgisayarda test edilir): sinyal kalitesi, güvenlik
// türü adı, ping istatistiği, ICMP yanıtlarının ayrıştırılması.
#include <vector>

// Taramada görülen bir erişim noktası (aynı ada sahip her BSSID ayrı satır)
struct WifiAp {
  String ssid;  // boşsa gizli ağ
  uint8_t bssid[6] = {0};
  int8_t rssi = 0;
  uint8_t ch = 0, auth = 0;
};

// RSSI'dan kabaca kalite yüzdesi: -100 dBm ve altı %0, -50 dBm ve üstü %100 (doğrusal)
static int rssiQuality(int rssi) { return rssi <= -100 ? 0 : rssi >= -50 ? 100 : 2 * (rssi + 100); }
static const char* rssiWord(int rssi) { return rssi >= -60 ? "iyi" : rssi >= -72 ? "orta" : rssi >= -82 ? "zayıf" : "çok zayıf"; }

// ESP-IDF wifi_auth_mode_t sırası (IDF 4.4)
static const char* authText(uint8_t a) {
  static const char* const T[] = {"açık", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA2-Kurumsal", "WPA3", "WPA2/WPA3", "WAPI"};
  return a < sizeof(T) / sizeof(T[0]) ? T[a] : "bilinmiyor";
}

static String macText(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

// Ping özeti: gönderilen, alınan, kayıp yüzdesi, en az / ortalama / en çok süre
struct PingStats {
  int sent = 0, recv = 0;
  uint32_t minMs = 0, maxMs = 0, sumMs = 0;
  void add(bool ok, uint32_t ms) {
    sent++;
    if (!ok) return;
    if (!recv || ms < minMs) minMs = ms;
    if (ms > maxMs) maxMs = ms;
    sumMs += ms;
    recv++;
  }
  int lossPct() const { return sent ? (sent - recv) * 100 / sent : 0; }
  String summary() const {
    String s = String(sent) + " gönderildi, " + String(recv) + " alındı, %" + String(lossPct()) + " kayıp";
    if (recv) s += " · en az/ort/en çok " + String(minMs) + "/" + String((sumMs + recv / 2) / recv) + "/" + String(maxMs) + " ms";
    return s;
  }
};

// Ham ICMP soketinden gelen paketi (IP başlığı dahil) çözer. Kendi isteğimize (kimlik + sıra) ait bir
// yankı yanıtı (0), zaman aşımı (11) ya da ulaşılamaz (3) ise türünü, değilse -1 döndürür.
// Zaman aşımı ve ulaşılamaz iletilerinin içinde bizim gönderdiğimiz IP + ICMP başlığı bulunur.
static int icmpMatch(const uint8_t* p, size_t n, uint16_t id, uint16_t seq) {
  if (n < 20 || (p[0] >> 4) != 4) return -1;
  size_t ihl = (p[0] & 15) * 4;
  if (ihl < 20 || n < ihl + 8 || p[9] != 1) return -1;  // ICMP değil
  const uint8_t* ic = p + ihl;
  uint8_t type = ic[0];
  auto u16 = [](const uint8_t* b) { return (uint16_t)(b[0] << 8 | b[1]); };
  if (type == 0) return (u16(ic + 4) == id && u16(ic + 6) == seq) ? 0 : -1;
  if (type != 11 && type != 3) return -1;
  const uint8_t* in = ic + 8;  // içteki (bizim) IP başlığı
  if (n < ihl + 8 + 20 || (in[0] >> 4) != 4) return -1;
  size_t ihl2 = (in[0] & 15) * 4;
  if (n < ihl + 8 + ihl2 + 8 || in[9] != 1) return -1;
  const uint8_t* ic2 = in + ihl2;
  if (ic2[0] != 8 || u16(ic2 + 4) != id || u16(ic2 + 6) != seq) return -1;
  return type;
}

// --- Ortak durum: tarama ayrıntısı ve ağ aracı işinin çıktısı ---------------
static std::vector<WifiAp> wifiAps;          // son ayrıntılı tarama (wifiScanFull istenince)  mtx
static volatile bool wifiScanFull = false;   // sıradaki tarama her BSSID'yi ayrıntılı saklasın
enum NdJob : uint8_t { ND_SCAN, ND_PING, ND_DNS, ND_TCP, ND_TRACE, ND_TEST };
static std::vector<String> ndOut;            // işin ürettiği, henüz okunmamış satırlar      mtx
static volatile bool ndToSerial = false;     // iş seri porttan başlatıldı: çıktı oraya
static bool ndConsented = false;             // bu açılışta ağ araçları için onay verildi mi

static void ndEmit(const String& s) {
  Lock l;
  if (ndOut.size() < 200) ndOut.push_back(s);
}
static void ndDrain(std::vector<String>& out) {
  Lock l;
  out.insert(out.end(), ndOut.begin(), ndOut.end());
  ndOut.clear();
}
