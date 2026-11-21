#pragma once
// DAXEN VANE sistem katmanı · Ağ teşhisi: ayrıntılı tarama, ping, DNS, TCP bağlantı testi,
// traceroute ve bağlantı testi. Tarama ağ görevine yaptırılır (süreç açılmaz). Diğer işler iş
// süresince var olan "ağ-araç" sürecinde çalışır (6 KB yığın); bellek yetmezse bulutun canlı akışı
// duraklatılır (netHold) ve iş bellek açılınca başlar, bitince akış geri gelir. Çıktı satır satır
// ndOut'a yazılır, Terminal ve Ağ uygulaması oradan okur. Yalnızca
// kullanıcının istediği adreslere yanıt isteği gönderilir; tarama pasiftir, ağa müdahale edilmez.
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.
#include <lwip/netdb.h>
#include <lwip/inet_chksum.h>
#include <lwip/prot/icmp.h>

static int ndPid = -1;
static String ndErr;
static bool ndPending = false, ndScanPending = false;  // bellek bekleniyor / tarama sürüyor
static uint32_t ndPendingAt = 0, ndScanBefore = 0;
static const uint32_t ND_STACK = 6144;
static uint8_t ndJob = ND_SCAN;
static String ndHost;
static int ndArg = 0;
static Proc* volatile ndProcP = nullptr;

static bool ndStop() { return ndProcP && ndProcP->killReq; }

// Ad ya da IP'yi IPv4 adresine çevirir; ms: süre (DNS sorgusu yapıldıysa)
static bool ndResolve(const String& host, ip4_addr_t& out, uint32_t& ms, String& err) {
  uint32_t t0 = millis();
  if (ip4addr_aton(host.c_str(), &out)) {
    ms = 0;
    return true;
  }
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_INET;
  int r = lwip_getaddrinfo(host.c_str(), nullptr, &hints, &res);
  ms = millis() - t0;
  if (r != 0 || !res) {
    err = "ad çözülemedi";
    return false;
  }
  out.addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
  lwip_freeaddrinfo(res);
  return true;
}

static String ndIp(const ip4_addr_t& a) {
  char b[16];
  ip4addr_ntoa_r(&a, b, sizeof b);
  return b;
}

// Tek bir ICMP yankı isteği gönderir (ttl ile), yanıtı bekler. Dönüş: 0 yankı, 11 zaman aşımı
// (ara yönlendirici), 3 ulaşılamaz, -1 yanıt yok. from: yanıtı veren, ms: süre
static int ndProbe(int sock, const ip4_addr_t& dst, uint8_t ttl, uint16_t id, uint16_t seq, uint32_t timeoutMs, ip4_addr_t& from,
                   uint32_t& ms) {
  int t = ttl;
  lwip_setsockopt(sock, IPPROTO_IP, IP_TTL, &t, sizeof t);
  uint8_t pkt[sizeof(struct icmp_echo_hdr) + 24];
  struct icmp_echo_hdr* h = (struct icmp_echo_hdr*)pkt;
  memset(pkt, 0, sizeof pkt);
  ICMPH_TYPE_SET(h, ICMP_ECHO);
  h->id = lwip_htons(id);
  h->seqno = lwip_htons(seq);
  memcpy(pkt + sizeof(*h), "DAXEN VANE ag teshisi..", 24);
  h->chksum = inet_chksum(pkt, sizeof pkt);
  struct sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = dst.addr;
  uint32_t t0 = millis();
  if (lwip_sendto(sock, pkt, sizeof pkt, 0, (struct sockaddr*)&to, sizeof to) <= 0) return -1;
  uint8_t buf[128];
  while (millis() - t0 < timeoutMs && !ndStop()) {
    struct timeval tv = {0, 200000};  // iptal 200 ms içinde fark edilsin
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in src = {};
    socklen_t sl = sizeof src;
    int n = lwip_recvfrom(sock, buf, sizeof buf, 0, (struct sockaddr*)&src, &sl);
    if (n <= 0) continue;
    int type = icmpMatch(buf, n, id, seq);
    if (type < 0) continue;
    ms = millis() - t0;
    from.addr = src.sin_addr.s_addr;
    return type;
  }
  return -1;
}

static int ndIcmpSocket() {
  int s = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (s < 0) ndEmit("Hata: ICMP soketi açılamadı");
  return s;
}

static void ndPing(const String& host, int count, PingStats* stats = nullptr) {
  ip4_addr_t dst, from;
  uint32_t dnsMs, ms;
  String err;
  if (!ndResolve(host, dst, dnsMs, err)) {
    ndEmit(host + ": " + err);
    return;
  }
  int s = ndIcmpSocket();
  if (s < 0) return;
  ndEmit("ping " + host + (host == ndIp(dst) ? String("") : " (" + ndIp(dst) + ")"));
  PingStats st;
  uint16_t id = esp_random() & 0xffff;
  for (int i = 0; i < count && !ndStop(); i++) {
    uint32_t t0 = millis();
    int r = ndProbe(s, dst, 64, id, i + 1, 1000, from, ms);
    bool ok = r == 0;
    st.add(ok, ms);
    if (ok) ndEmit("  yanıt " + ndIp(from) + "  " + String(ms) + " ms");
    else if (r == 3) ndEmit("  ulaşılamıyor (" + ndIp(from) + ")");
    else ndEmit("  yanıt yok");
    while (i + 1 < count && millis() - t0 < 1000 && !ndStop()) vTaskDelay(pdMS_TO_TICKS(50));
  }
  lwip_close(s);
  ndEmit("  " + st.summary());
  if (stats) *stats = st;
}

static void ndTrace(const String& host) {
  ip4_addr_t dst, from;
  uint32_t dnsMs, ms;
  String err;
  if (!ndResolve(host, dst, dnsMs, err)) {
    ndEmit(host + ": " + err);
    return;
  }
  int s = ndIcmpSocket();
  if (s < 0) return;
  ndEmit("traceroute " + ndIp(dst) + " (en fazla 20 adım, yanıt vermeyen adım *)");
  uint16_t id = esp_random() & 0xffff;
  for (int ttl = 1; ttl <= 20 && !ndStop(); ttl++) {
    int r = ndProbe(s, dst, ttl, id, ttl, 1500, from, ms);
    String n = (ttl < 10 ? " " : "") + String(ttl);
    if (r < 0) ndEmit("  " + n + "  *");
    else ndEmit("  " + n + "  " + ndIp(from) + "  " + String(ms) + " ms" + (r == 3 ? "  (ulaşılamıyor)" : ""));
    if (r == 0 || r == 3) break;
  }
  lwip_close(s);
}

static void ndDns(const String& host) {
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_INET;
  uint32_t t0 = millis();
  int r = lwip_getaddrinfo(host.c_str(), nullptr, &hints, &res);
  uint32_t ms = millis() - t0;
  ndEmit("dns " + host + " (sunucu " + WiFi.dnsIP(0).toString() + ")");
  if (r != 0 || !res) {
    ndEmit("  çözülemedi (" + String(ms) + " ms)");
    return;
  }
  int k = 0;
  for (struct addrinfo* a = res; a && k < 4; a = a->ai_next, k++) {
    ip4_addr_t ip;
    ip.addr = ((struct sockaddr_in*)a->ai_addr)->sin_addr.s_addr;
    ndEmit("  " + ndIp(ip));
  }
  lwip_freeaddrinfo(res);
  ndEmit("  " + String(ms) + " ms");
}

static bool ndTcp(const String& host, int port) {
  ip4_addr_t dst;
  uint32_t dnsMs;
  String err;
  if (!ndResolve(host, dst, dnsMs, err)) {
    ndEmit("  " + host + ": " + err);
    return false;
  }
  WiFiClient c;
  uint32_t t0 = millis();
  bool ok = c.connect(IPAddress(dst.addr), port, 4000);
  uint32_t ms = millis() - t0;
  c.stop();
  ndEmit("  tcp " + host + ":" + String(port) + (ok ? "  bağlandı " + String(ms) + " ms" : String("  bağlanamadı (4 sn)")));
  return ok;
}

// Bağlantı testi: ağ geçidi, DNS, internet (TCP 443)
static void ndTest() {
  ndEmit("Bağlantı testi");
  String gw = WiFi.gatewayIP().toString();
  PingStats st;
  ndEmit("1) Ağ geçidi " + gw);
  ndPing(gw, 3, &st);
  if (ndStop()) return;
  ndEmit("2) DNS");
  ip4_addr_t ip;
  uint32_t ms;
  String err;
  bool dnsOk = ndResolve("github.com", ip, ms, err);
  ndEmit(dnsOk ? "  github.com → " + ndIp(ip) + "  " + String(ms) + " ms" : "  github.com: " + err);
  if (ndStop()) return;
  ndEmit("3) İnternet");
  bool netOk = dnsOk && ndTcp(ndIp(ip), 443);
  ndEmit(String("Sonuç: ") + (st.recv ? "ağ geçidi var" : "ağ geçidi yanıt vermiyor") + " · DNS " + (dnsOk ? "çalışıyor" : "çalışmıyor") +
         " · internet " + (netOk ? "var" : "yok"));
}

static void ndScanReport() {
  std::vector<WifiAp> aps;
  {
    Lock l;
    aps = wifiAps;
  }
  ndEmit(String(aps.size()) + " erişim noktası (güçlüden zayıfa):");
  for (auto& a : aps) {
    String s = a.ssid.length() ? a.ssid : String("(gizli)");
    ndEmit("  " + String(a.rssi) + " dBm " + String(rssiQuality(a.rssi)) + "%  k" + String(a.ch) + "  " + authText(a.auth) + "  " + s);
    ndEmit("      " + macText(a.bssid));
  }
}

static void ndProc(Proc* p) {
  ndProcP = p;
  if (WiFi.status() != WL_CONNECTED) {
    ndEmit("Wi-Fi'ye bağlı değil");
  } else {
    switch (ndJob) {
      case ND_PING: ndPing(ndHost, ndArg); break;
      case ND_DNS: ndDns(ndHost); break;
      case ND_TCP: ndTcp(ndHost, ndArg); break;
      case ND_TRACE: ndTrace(ndHost); break;
      case ND_TEST: ndTest(); break;
    }
  }
  if (p->killReq) ndEmit("Durduruldu");
  ndProcP = nullptr;
}

static bool ndBusy() { return ndPending || ndScanPending || kAlive(ndPid); }
static String ndError() { return ndErr; }
static bool netLiveOpen() { return live.c != nullptr; }

// Bellek yeterliyse işi başlatır; değilse bulut duraklatılmış olarak bekler (en fazla 8 sn).
// Biten işin süreç kaydını kapatıp bulutu geri bırakır. Saat servisinden ve ekranlardan çağrılır.
static void ndPoll() {
  if (ndScanPending && (wifiScanAt != ndScanBefore || millis() - ndPendingAt > 15000)) {
    ndScanPending = false;
    if (wifiScanAt != ndScanBefore) ndScanReport();
    else ndEmit("Tarama bitmedi");
  }
  if (ndPending) {
    String why;
    bool memOk = memAsk(MJ_NETTOOL, ND_STACK, ND_STACK, &why);  // os/membudget.h
    if (memOk) {
      ndPending = false;
      ndPid = kSpawn("ağ-araç", ndProc, ND_STACK, true, K_PROC_PRIO, 1);
      if (ndPid < 0) ndEmit("Süreç açılamadı");
    } else if (millis() - ndPendingAt > 8000 || why.startsWith("Aynı anda")) {
      ndPending = false;  // bellek beklenince açılmadı ya da başka ağır işler açık
      ndEmit(why);
    }
  }
  if (!ndPending && !kAlive(ndPid)) {
    ndPid = -1;
    netHold &= ~HOLD_NETTOOL;
  }
}

static bool ndStart(uint8_t job, const String& host, int arg) {
  if (ndBusy()) {
    ndErr = "Önceki ağ işi sürüyor (durdurmak için: stop)";
    return false;
  }
  static const char* const NAMES[] = {"tarama", "ping", "dns", "tcp", "traceroute", "test"};
  sysLog("ağ", String("araç: ") + NAMES[job] + (host.length() ? " " + host : String("")));
  ndPendingAt = millis();
  if (job == ND_SCAN) {  // taramayı ağ görevi yapar; süreç ve ek bellek gerekmez
    ndEmit("Taranıyor…");
    ndScanBefore = wifiScanAt;
    ndScanPending = true;
    wifiScanFull = true;
    wifiScanReq = true;
    return true;
  }
  ndJob = job;
  ndHost = host;
  ndArg = arg;
  ndPending = true;
  if (memDecide(sysHeapFree(), sysHeapMaxAlloc(), ND_STACK, ND_STACK, 0, MJ_NETTOOL) != MV_OK) {
    netHold |= HOLD_NETTOOL;  // bulut iş bitene kadar bekler; ndPoll bırakır
    ndEmit("Bellek açılıyor (bulut bağlantısı iş boyunca duraklatıldı)…");
  }
  ndPoll();
  return true;
}

static void ndCancel() {
  ndPending = false;
  ndScanPending = false;
  if (kAlive(ndPid)) kStop(ndPid);
  ndPoll();
}

static void ndConnInfo(std::vector<String>& o) {
  if (WiFi.status() != WL_CONNECTED) {
    o.push_back("Wi-Fi'ye bağlı değil");
    return;
  }
  int r = WiFi.RSSI();
  o.push_back("Ağ: " + WiFi.SSID());
  o.push_back("Sinyal: " + String(r) + " dBm · %" + String(rssiQuality(r)) + " · " + rssiWord(r));
  o.push_back("BSSID: " + WiFi.BSSIDstr() + " · kanal " + String(WiFi.channel()));
  o.push_back("Adres: " + WiFi.localIP().toString() + " / " + WiFi.subnetMask().toString());
  o.push_back("Ağ geçidi: " + WiFi.gatewayIP().toString());
  String d2 = WiFi.dnsIP(1).toString();
  o.push_back("DNS: " + WiFi.dnsIP(0).toString() + (d2 != "0.0.0.0" ? " · " + d2 : String("")));
  o.push_back("Kartın MAC'i: " + WiFi.macAddress());
}

static void ndSaved(std::vector<String>& o) {
  Lock l;
  if (wifiKnown.empty()) o.push_back("Kayıtlı ağ yok");
  for (auto& k : wifiKnown) o.push_back("  " + k.ssid + (k.pass.length() ? "  (şifreli)" : "  (açık)"));
}
