#pragma once
// DAXEN VANE sistem katmanı · Web sunucusu (Dosya aktarımı, hesap sayfası).
// main.cpp'den sırayla eklenir (tek çeviri birimi); kendinden önceki dosyalardaki tanımları kullanır.

// ============================================================================
// Web sunucusu: telefondan tarayıcıyla SD karta dosya gönderme ve hesap bilgileri
// ============================================================================
// Yalnızca Dosya aktarımı ekranı (ya da Ayarlar → Bulut hesabı → tarayıcıdan gir) açıkken çalışır.
// Aynı Wi-Fi ağındaki telefon http://<kartın adresi>/ adresini açar.
//
// Erişim: sunucu her açılışta 6 haneli bir giriş kodu üretir ve kartın ekranında gösterir. Tarayıcı
// kodla girer ve 128 bitlik rastgele oturum çerezi alır (HttpOnly, SameSite=Strict); kod ya da çerez
// olmadan hiçbir sayfa, indirme, yükleme ya da silme çalışmaz. 5 yanlış kodda giriş 60 sn kilitlenir.
// Sunucu kapanınca kod ve oturum silinir. Sistem klasörleri (/daxen, /daxen/sys, /calisma)
// web'den silinemez, içine yazılamaz.
//
// Sunucu çekirdekte ayrı bir süreçtir ("web"): büyük bir dosya gelirken arayüz donmaz. Sayfa
// dosyaları JavaScript ile tek tek ham PUT olarak gönderir (1,4 KB'lık parçalar, kartta 4 KB
// tamponla SD'ye yazılır); çok parçalı form yalnızca JavaScript yoksa kullanılır. Gelen dosya
// bitene kadar ".part" adıyla yazılır, yarım dosya asıl adıyla görünmez.
static WebServer* web = nullptr;
static String webMsg;                // son işlem (ekranda gösterilir)        mtx
static String webUpName;             // yüklenen dosya                         mtx
static volatile uint32_t webUpBytes = 0, webUpTotal = 0;
static volatile int webUploads = 0;
static volatile bool webUpOk = false;
static File webUpFile;
static String webUpPath;
static uint8_t* webBuf = nullptr;
static size_t webBufN = 0;
static const size_t WEB_BUF = 4096;
static int webPid = -1;
static Proc* volatile webProcP = nullptr;
static char webCodeStr[7] = "";       // giriş kodu (ekranda gösterilir)
static char webTok[33] = "";          // geçerli oturum (boşsa kimse girmemiş)
static uint8_t webFails = 0;          // art arda yanlış kod
static uint32_t webLockUntil = 0;     // giriş kilidi (millis)
static bool webUpDenied = false;      // süren yükleme yetkisiz: baytlar yok sayılır

static const char WEB_HEAD[] PROGMEM = R"(<!doctype html><html lang=tr><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>DAXEN VANE</title><style>
body{margin:0;background:#050505;color:#C5C9CE;font:15px/1.45 system-ui,sans-serif}
main{max-width:640px;margin:auto;padding:18px}h1{font:600 13px system-ui;letter-spacing:.3em;color:#858B91;margin:0 0 16px}
h2{font-size:17px;margin:20px 0 8px}a{color:#C5C9CE;text-decoration:none}
.row{display:flex;align-items:center;gap:10px;padding:10px 12px;border-bottom:1px solid #30353A}
.row span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.mut{color:#858B91;font-size:13px}
.box{background:#151515;border:1px solid #30353A;border-radius:4px;padding:12px;margin:12px 0}
input,button{font:inherit;background:#101010;color:#C5C9CE;border:1px solid #30353A;border-radius:3px;padding:8px 10px}
button{border-color:#7892A5;cursor:pointer}input[type=text],input[type=password]{width:100%;box-sizing:border-box;margin:4px 0 10px}
.del{border:none;background:none;color:#555B60;padding:4px}form.inl{display:inline}.path{color:#7892A5}
</style></head><body><main><h1>DAXEN VANE</h1>)";

static String webEsc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";  // tek tırnaklı özniteliklerde de güvenli
    else o += c;
  }
  return o;
}

static String webUrlEnc(const String& s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == '~') o += (char)c;
    else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

static String webSafePath(String p) {
  if (!p.startsWith("/")) p = "/" + p;
  if (p.indexOf("..") >= 0) p = "/";
  while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

// Sistem klasörleri: web'den silinemez, içlerine dosya yazılamaz (kuyruk, günlük, ayar dosyaları)
static bool webSysPath(const String& p) {
  String l = trLower(p);
  return l == "/daxen" || l == "/daxen/sys" || l.startsWith("/daxen/sys/") || l == "/calisma" || l.startsWith("/calisma/");
}
static bool webSysDir(const String& d) {  // bu klasöre yazmak yasak mı
  String l = trLower(d);
  return l == "/daxen/sys" || l.startsWith("/daxen/sys/") || l == "/calisma" || l.startsWith("/calisma/");
}

static void webRedirect(const String& to);

// Oturum çerezi geçerli mi (sabit süreli karşılaştırma)
static bool webAuthed() {
  if (!webTok[0]) return false;
  String c = web->header("Cookie");
  int i = c.indexOf("dv=");
  if (i < 0 || c.length() < (unsigned)(i + 3 + 32)) return false;
  uint8_t diff = 0;
  for (int k = 0; k < 32; k++) diff |= (uint8_t)(c[i + 3 + k] ^ webTok[k]);
  return diff == 0;
}

static void webLoginPage(const String& msg) {
  String next = web->uri() == "/giris" ? String("/") : web->uri();
  if (next == "/" && web->hasArg("d")) next += "?d=" + webUrlEnc(webSafePath(web->arg("d")));
  web->send(web->uri() == "/giris" ? 200 : 401, "text/html; charset=utf-8",
            String(FPSTR(WEB_HEAD)) + "<h2>Giriş</h2><p class=mut>Kartın ekranındaki 6 haneli kodu yaz.</p>" +
                (msg.length() ? "<p>" + webEsc(msg) + "</p>" : String("")) +
                "<form method=post action='/giris' class=box><input type=text name=k inputmode=numeric autocomplete=off maxlength=6 autofocus>"
                "<input type=hidden name=r value='" + webEsc(next) + "'><button>Gir</button></form></main></body></html>");
}

// Sayfa isteklerinde: oturum yoksa giriş sayfası gösterilir
static bool webGatePage() {
  if (webAuthed()) return true;
  webLoginPage("");
  return false;
}

// İşlem isteklerinde (indirme, silme, klasör, hesap kaydı): oturum yoksa 401
static bool webGate() {
  if (webAuthed()) return true;
  web->send(401, "text/plain; charset=utf-8", "Giriş gerekli");
  return false;
}

static void webLoginPost() {
  if ((int32_t)(millis() - webLockUntil) < 0) {
    webLoginPage("Çok fazla yanlış deneme. " + String((webLockUntil - millis()) / 1000 + 1) + " sn sonra tekrar dene.");
    return;
  }
  String k = web->arg("k");
  k.trim();
  bool ok = webCodeStr[0] && k.length() == 6;
  for (int i = 0; ok && i < 6; i++) ok = k[i] == webCodeStr[i];
  if (!ok) {
    webFails++;
    sysLog("web", "yanlış giriş kodu (" + String(webFails) + "/5)");
    if (webFails >= 5) {
      webFails = 0;
      webLockUntil = millis() + 60000;
      sysLog("web", "giriş 60 sn kilitlendi");
    }
    webLoginPage("Kod yanlış.");
    return;
  }
  webFails = 0;
  for (int i = 0; i < 4; i++) snprintf(webTok + i * 8, 9, "%08x", (unsigned)esp_random());
  sysLog("web", "tarayıcı giriş yaptı");
  String r = web->arg("r");
  if (!r.startsWith("/") || r.startsWith("//")) r = "/";  // yalnızca bu sunucunun içine dön
  web->sendHeader("Set-Cookie", String("dv=") + webTok + "; Path=/; HttpOnly; SameSite=Strict");
  webRedirect(r);
}

static void webPage() {
  if (!webGatePage()) return;
  if (!sdOk) {
    web->send(200, "text/html; charset=utf-8", String(FPSTR(WEB_HEAD)) + "<p>SD kart takılı değil.</p></main></body></html>");
    return;
  }
  String dir = webSafePath(web->hasArg("d") ? web->arg("d") : String("/"));
  web->setContentLength(CONTENT_LENGTH_UNKNOWN);
  web->send(200, "text/html; charset=utf-8", "");
  web->sendContent(FPSTR(WEB_HEAD));
  String h = "<div class=box><form id=fm method=post enctype=multipart/form-data action='/up?d=" + webUrlEnc(dir) +
             "'><b>Dosya gönder</b><p class=mut>Birden çok dosya seçebilirsin.</p>"
             "<input type=file name=f id=fi multiple> <button id=gb>Gönder</button>"
             "<div id=pt class=mut style='margin-top:8px'></div><div style='height:3px;background:#30353A;margin-top:6px'>"
             "<div id=pb style='height:3px;width:0;background:#7892A5'></div></div></form></div>"
             "<script>fm.onsubmit=async e=>{e.preventDefault();const fs=[...fi.files];if(!fs.length)return;gb.disabled=true;"
             "for(let i=0;i<fs.length;i++){const x=fs[i];const ok=await new Promise(r=>{const q=new XMLHttpRequest();"
             "q.open('PUT','/put?d=" + webUrlEnc(dir) + "&n='+encodeURIComponent(x.name));"
             "q.setRequestHeader('Content-Type','application/octet-stream');"
             "q.upload.onprogress=v=>{if(v.lengthComputable){const k=Math.floor(v.loaded*100/v.total);pb.style.width=k+'%';"
             "pt.textContent=(i+1)+' / '+fs.length+' · '+x.name+' · %'+k}};"
             "q.onload=()=>r(q.status==200);q.onerror=()=>r(false);q.send(x)});"
             "if(!ok){pt.textContent='Gönderilemedi: '+x.name+'. Kart bu ekranda ve aynı Wi-Fi ağında mı?';gb.disabled=false;return}}"
             "location.reload()}</script>";
  h += "<h2>Klasör: <span class=path>" + webEsc(dir) + "</span></h2>";
  if (dir != "/") h += "<div class=row><a href='/?d=" + webUrlEnc(dirName(dir)) + "'>&#8592; Üst klasör</a></div>";
  web->sendContent(h);
  File d = SD.open(dir);
  std::vector<std::pair<String, uint32_t>> dirs, files;
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f && dirs.size() + files.size() < 500; f = d.openNextFile()) {
      String n = baseName(f.name());
      if (!n.startsWith(".")) (f.isDirectory() ? dirs : files).push_back({n, (uint32_t)f.size()});
      f.close();
    }
    d.close();
  }
  auto byName = [](const std::pair<String, uint32_t>& a, const std::pair<String, uint32_t>& b) { return trLower(a.first) < trLower(b.first); };
  std::sort(dirs.begin(), dirs.end(), byName);
  std::sort(files.begin(), files.end(), byName);
  for (auto& e : dirs) {
    String p = joinPath(dir, e.first);
    web->sendContent("<div class=row>&#128193;<span><a href='/?d=" + webUrlEnc(p) + "'>" + webEsc(e.first) + "</a></span>" +
                     (webSysPath(p) ? String("<span class=mut style='flex:none'>sistem</span>")
                                    : "<form class=inl method=post action='/del?f=" + webUrlEnc(p) +
                                          "' onsubmit='return confirm(\"Klasör ve içindekiler silinsin mi?\")'><button class=del>Sil</button></form>") +
                     "</div>");
  }
  for (auto& e : files) {
    String p = joinPath(dir, e.first);
    web->sendContent("<div class=row><span><a href='/dl?f=" + webUrlEnc(p) + "'>" + webEsc(e.first) + "</a></span><span class=mut style='flex:none'>" +
                     fmtBytes(e.second) + "</span>" +
                     (webSysPath(p) ? String("")
                                    : "<form class=inl method=post action='/del?f=" + webUrlEnc(p) +
                                          "' onsubmit='return confirm(\"Silinsin mi?\")'><button class=del>Sil</button></form>") +
                     "</div>");
  }
  if (dirs.empty() && files.empty()) web->sendContent("<p class=mut>Klasör boş.</p>");
  web->sendContent("<div class=box><form method=post action='/mkdir?d=" + webUrlEnc(dir) +
                   "'><b>Yeni klasör</b><input type=text name=n placeholder='Klasör adı'><button>Oluştur</button></form></div>"
                   "<p class=mut><a href='/hesap'>Bulut hesabı bilgileri &#8594;</a></p></main></body></html>");
  web->sendContent("");
}

static void webRedirect(const String& to) {
  web->sendHeader("Location", to);
  web->send(303, "text/plain", "");
}

static void webSetMsg(const String& m) {
  Lock l;
  webMsg = m;
}

static void webFlush() {
  if (webUpFile && webBufN) webUpFile.write(webBuf, webBufN);
  webBufN = 0;
}

static void webBeginFile(const String& dir, const String& rawName, uint32_t total) {
  String name = safeName(baseName(rawName));
  if (!name.length()) name = "dosya";
  {
    Lock l;
    webUpName = name;
  }
  webUpBytes = 0;
  webUpTotal = total;
  webUpOk = false;
  webUpPath = joinPath(dir, name);
  String part = webUpPath + ".part";
  if (SD.exists(part)) SD.remove(part);
  webUpFile = SD.open(part, FILE_WRITE);
  if (!webBuf) webBuf = (uint8_t*)malloc(WEB_BUF);
  webBufN = 0;
  webSetMsg(webUpFile ? "Alınıyor: " + name : "Yazılamadı: " + name);
}

static void webWrite(const uint8_t* d, size_t n) {
  webUpBytes += n;
  if (!webUpFile) return;
  if (!webBuf) {
    webUpFile.write(d, n);
    return;
  }
  while (n) {
    size_t k = std::min(n, WEB_BUF - webBufN);
    memcpy(webBuf + webBufN, d, k);
    webBufN += k;
    d += k;
    n -= k;
    if (webBufN == WEB_BUF) webFlush();
  }
}

static void webEndFile(bool ok) {
  webFlush();
  bool had = (bool)webUpFile;
  if (webUpFile) webUpFile.close();
  String part = webUpPath + ".part", name;
  {
    Lock l;
    name = webUpName;
  }
  if (ok && had) {
    if (SD.exists(webUpPath)) SD.remove(webUpPath);
    ok = SD.rename(part, webUpPath);
  } else {
    ok = false;
    if (SD.exists(part)) SD.remove(part);
  }
  if (ok) {
    webUploads++;
    webSetMsg("Alındı: " + name + " (" + fmtBytes(webUpBytes) + ")");
  } else {
    webSetMsg((had ? "Yarıda kesildi: " : "Yazılamadı: ") + name);
  }
  webUpOk = ok;
  {
    Lock l;
    webUpName = "";
  }
  dataDirty = true;
}

// Çok parçalı form (JavaScript'siz yedek yol)
static void webUploadChunk() {
  HTTPUpload& up = web->upload();
  if (up.status == UPLOAD_FILE_START) {
    String d = webSafePath(web->hasArg("d") ? web->arg("d") : String("/"));
    webUpDenied = !webAuthed() || webSysDir(d);
    if (webUpDenied) return;
    webBeginFile(d, up.filename, web->header("Content-Length").toInt());
  } else if (webUpDenied) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    webWrite(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    webEndFile(up.status == UPLOAD_FILE_END);
  }
}

// Ham PUT: sayfadaki JavaScript her dosyayı böyle gönderir (hızlı yol)
static void webRawChunk() {
  HTTPRaw& r = web->raw();
  if (r.status == RAW_START) {
    String d = webSafePath(web->arg("d"));
    webUpDenied = !webAuthed() || webSysDir(d);
    if (webUpDenied) return;
    webBeginFile(d, web->arg("n"), web->clientContentLength());
  } else if (webUpDenied) {
    return;
  } else if (r.status == RAW_WRITE) {
    webWrite(r.buf, r.currentSize);
    if (webProcP && webProcP->killReq) {  // ekrandan çıkıldı: bağlantı kapatılır, yükleme yarıda kesilir
      int fd = web->client().fd();
      if (fd >= 0) lwip_shutdown(fd, SHUT_RDWR);
    }
  } else {
    webEndFile(r.status == RAW_END);
  }
}

static bool fRemoveAll(const String& path);  // apps/files.h

// Kayıtlı bilgiler sayfaya geri yazılmaz; boş bırakılan alan değişmez.
static void webAccountPage() {
  if (!webGatePage()) return;
  auto field = [](const char* label, const char* name, const char* type, bool saved) {
    return String(label) + (saved ? " <span class=mut>(kayıtlı; değişmeyecekse boş bırak)</span>" : "") + "<input type=" + type +
           " name=" + name + " autocomplete=off>";
  };
  String h = String(FPSTR(WEB_HEAD)) + "<h2>Bulut hesabı</h2><p class=mut>Telefondaki uygulamaya girdiğin Firebase bilgilerinin aynısı.</p>"
             "<form method=post action='/hesap' class=box>" + field("Veritabanı adresi", "db", "text", cfg.dbUrl.length()) +
             field("Web API anahtarı", "key", "password", cfg.apiKey.length()) + field("E-posta", "mail", "text", cfg.email.length()) +
             field("Şifre", "pass", "password", cfg.password.length()) +
             "<button>Kaydet</button></form><p><a href='/'>&#8592; Dosyalar</a></p></main></body></html>";
  web->send(200, "text/html; charset=utf-8", h);
}

// Sunucu süreci: istekleri işler; uzun bir yükleme sürerken yalnızca bu süreç meşguldür
static void webProc(Proc* p) {
  webProcP = p;
  while (!p->killReq) {
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t t0 = micros();
      web->handleClient();
      kAddBusy(p, micros() - t0);
    }
    vTaskDelay(pdMS_TO_TICKS(4));
  }
  webProcP = nullptr;
}

static bool webStart() {
  if (web) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  // Süreç yığını (10 KB) + yükleme tamponu (4 KB) + sunucu; 18 KB taban korunur. Yetmezse bulutun
  // canlı akışı duraklatılır (netHold), ekran birkaç saniye sonra yeniden dener.
  if (sysHeapFree() < (10 + 4 + 4 + 18) * 1024 || sysHeapMaxAlloc() < 11 * 1024) {
    netHold |= HOLD_WEB;
    return false;
  }
  web = new (std::nothrow) WebServer(80);
  if (!web) return false;
  const char* hdrs[] = {"Content-Length", "Cookie"};
  web->collectHeaders(hdrs, 2);
  for (int i = 0; i < 6; i++) webCodeStr[i] = '0' + esp_random() % 10;
  webCodeStr[6] = 0;
  webTok[0] = 0;
  webFails = 0;
  web->on("/", HTTP_GET, webPage);
  web->on("/giris", HTTP_GET, []() { webLoginPage(""); });
  web->on("/giris", HTTP_POST, webLoginPost);
  web->on("/dl", HTTP_GET, []() {
    if (!webGate()) return;
    String p = webSafePath(web->arg("f"));
    File f = SD.open(p);
    if (!f || f.isDirectory()) {
      web->send(404, "text/plain", "yok");
      return;
    }
    web->sendHeader("Content-Disposition", "attachment; filename=\"" + baseName(p) + "\"");
    web->streamFile(f, "application/octet-stream");
    f.close();
  });
  web->on("/up", HTTP_POST, []() { webRedirect("/?d=" + webUrlEnc(webSafePath(web->arg("d")))); }, webUploadChunk);
  web->on("/put", HTTP_PUT, []() {
    if (webUpDenied) web->send(403, "text/plain; charset=utf-8", webAuthed() ? "Sistem klasörüne yazılamaz" : "Giriş gerekli");
    else web->send(webUpOk ? 200 : 500, "text/plain", webUpOk ? "tamam" : "hata");
  }, webRawChunk);
  web->on("/del", HTTP_POST, []() {
    if (!webGate()) return;
    String p = webSafePath(web->arg("f"));
    if (webSysPath(p)) {
      webSetMsg("Sistem klasörü silinemez");
    } else if (p != "/") {
      fRemoveAll(p);
      webSetMsg("Silindi: " + baseName(p));
      dataDirty = true;
    }
    webRedirect("/?d=" + webUrlEnc(dirName(p)));
  });
  web->on("/mkdir", HTTP_POST, []() {
    if (!webGate()) return;
    String d = webSafePath(web->arg("d"));
    String n = safeName(web->arg("n"));
    if (n.length() && !webSysDir(d)) SD.mkdir(joinPath(d, n));
    webRedirect("/?d=" + webUrlEnc(d));
  });
  web->on("/hesap", HTTP_GET, webAccountPage);
  web->on("/hesap", HTTP_POST, []() {
    if (!webGate()) return;
    String db = normalizeDbUrl(web->arg("db")), key = web->arg("key"), mail = web->arg("mail"), pass = web->arg("pass");
    key.trim();
    mail.trim();
    accountSet(db.length() ? db : cfg.dbUrl, key.length() ? key : cfg.apiKey, mail.length() ? mail : cfg.email,
               pass.length() ? pass : cfg.password);
    webSetMsg("Hesap bilgileri kaydedildi");
    sysLog("web", "bulut hesabı tarayıcıdan değiştirildi");
    dataDirty = true;
    web->send(200, "text/html; charset=utf-8", String(FPSTR(WEB_HEAD)) + "<p>Kaydedildi. Kart buluta bağlanmayı deniyor.</p><p><a href='/'>Dosyalar</a></p></main></body></html>");
  });
  web->onNotFound([]() { web->send(404, "text/plain", "yok"); });
  web->begin();
  webSetMsg("");
  webPid = kSpawn("web", webProc, 10 * 1024, false, K_PROC_PRIO, 1);
  if (webPid < 0) {  // süreç açılamadı (bellek)
    web->stop();
    delete web;
    web = nullptr;
    sysLog("web", "açılamadı: bellek yetersiz");
    return false;
  }
  sysLog("web", "sunucu açıldı");
  return true;
}

static void webStop() {
  netHold &= ~HOLD_WEB;
  if (!web) return;
  kStop(webPid);
  if (!kWait(webPid, 8000)) return;  // süreç hâlâ sunucuyu kullanıyor: silinmez (bir dahaki çağrıda)
  if (webUpFile) webEndFile(false);
  web->stop();
  delete web;
  web = nullptr;
  webPid = -1;
  free(webBuf);  // yükleme tamponu yalnızca sunucu açıkken tutulur
  webBuf = nullptr;
  webBufN = 0;
  webCodeStr[0] = 0;  // kod ve oturum sunucuyla birlikte biter
  webTok[0] = 0;
  sysLog("web", "sunucu kapandı");
}

static bool webRunning() { return web != nullptr; }
static String webCode() { return web ? String(webCodeStr) : String(); }

static String webStatus() {
  Lock l;
  if (webUpName.length()) {
    String s = webUpName + " · " + fmtBytes(webUpBytes);
    if (webUpTotal > webUpBytes) s += " / " + fmtBytes(webUpTotal);
    return s;
  }
  return webMsg;
}

static int webProgress() {
  {
    Lock l;
    if (!webUpName.length()) return -1;
  }
  if (webUpTotal == 0) return -1;
  return (int)std::min<uint64_t>(100, (uint64_t)webUpBytes * 100 / webUpTotal);
}
static int webUploadCount() { return webUploads; }

// Şehir arama (Hava durumu → ara). Engelleyici; ekranda "Aranıyor…" görünür.
static bool weatherSearch(const String& q, std::vector<GeoHit>& out) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String name = q;
  name.trim();
  int code;
  String resp = httpGetPlain("http://geocoding-api.open-meteo.com/v1/search?count=6&language=tr&format=json&name=" + webUrlEnc(name), code);
  if (code != 200) return false;
  JsonDocument d;
  if (deserializeJson(d, resp)) return false;
  for (JsonVariantConst r : d["results"].as<JsonArrayConst>()) {
    GeoHit h;
    h.name = String((const char*)(r["name"] | ""));
    String a1 = String((const char*)(r["admin1"] | "")), c = String((const char*)(r["country"] | ""));
    h.region = a1.length() && a1 != h.name ? a1 + ", " + c : c;
    h.lat = r["latitude"] | 0.0f;
    h.lon = r["longitude"] | 0.0f;
    if (h.name.length()) out.push_back(h);
  }
  return true;
}

static void splashLogo();
