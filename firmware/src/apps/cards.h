#pragma once
// Bilgi kartları: YKS formülleri ve ezber kartları. Kartı gör, çevir, "biliyorum / bilmiyorum" de;
// bilmediklerin daha sık gelir (Leitner kutuları). Kendi destelerini SD karta koyabilirsin:
// /daxen/kartlar/<deste>.txt, her satırda "ön | arka".
#include "../os/shell.h"
#include <SD.h>

#define CARDS_DIR "/daxen/kartlar"
struct BuiltinDeck {
  const char* name;
  const char* data;  // satırlar: ön|arka
};
static const BuiltinDeck BUILTIN_DECKS[] = {
    {"TYT Matematik",
     "a^m · a^n|a^(m+n)\n(a^m)^n|a^(m·n)\na^(−n)|1 / a^n\n√a · √b|√(a·b)\n(a+b)²|a² + 2ab + b²\n(a−b)²|a² − 2ab + b²\n"
     "a² − b²|(a−b)(a+b)\na³ − b³|(a−b)(a² + ab + b²)\na³ + b³|(a+b)(a² − ab + b²)\n1 + 2 + … + n|n(n+1) / 2\n"
     "1 + 3 + … + (2n−1)|n²\nAritmetik ortalama (a, b)|(a + b) / 2\nGeometrik ortalama (a, b)|√(a·b)\n|x| < a  (a > 0)|−a < x < a\n"
     "EKOK(a, b) · EBOB(a, b)|a · b"},
    {"Geometri",
     "Üçgende iç açılar toplamı|180°\nÇokgende dış açılar toplamı|360°\nn kenarlı çokgende iç açılar|(n − 2) · 180°\nPisagor bağıntısı|a² + b² = c²\n"
     "Üçgenin alanı|taban · yükseklik / 2\nEşkenar üçgenin alanı (kenar a)|a²√3 / 4\nDairenin alanı|πr²\nÇemberin çevresi|2πr\n"
     "Yamuğun alanı|(a + c) / 2 · h\nSilindirin hacmi|πr²h\nKoninin hacmi|πr²h / 3\nKürenin hacmi|4πr³ / 3\nKürenin yüzey alanı|4πr²\n"
     "30-60-90 üçgeninde kenarlar|1 : √3 : 2\n45-45-90 üçgeninde kenarlar|1 : 1 : √2"},
    {"Fizik",
     "Ortalama hız|v = Δx / Δt\nİvme|a = Δv / Δt\nNewton'un 2. yasası|F = m · a\nAğırlık|G = m · g\nİş|W = F · x\nKinetik enerji|Ek = ½ · m · v²\n"
     "Potansiyel enerji|Ep = m · g · h\nGüç|P = W / t\nKatı basıncı|P = F / A\nSıvı basıncı|P = h · d · g\nOhm yasası|V = I · R\n"
     "Elektriksel güç|P = V · I\nÖzkütle|d = m / V\nDalga hızı|v = λ · f\nMomentum|p = m · v"},
    {"Kimya",
     "Mol sayısı|n = m / M\nAvogadro sayısı|6,02 · 10²³\nNormal koşullarda 1 mol gaz|22,4 L\nİdeal gaz denklemi|P · V = n · R · T\n"
     "Molarite|n / V (mol/L)\npH|−log[H+]\n25 °C'de nötr çözeltinin pH'ı|7\nProton sayısı|Atom numarası\nKütle numarası|Proton + nötron sayısı\n"
     "Asitler suda|H+ iyonu verir\nBazlar suda|OH− iyonu verir\nKütlenin korunumu yasası|Lavoisier\nSabit oranlar yasası|Proust\n"
     "Katlı oranlar yasası|Dalton"},
    {"Edebiyat: eser – yazar",
     "Kuyucaklı Yusuf|Sabahattin Ali\nÇalıkuşu|Reşat Nuri Güntekin\nTutunamayanlar|Oğuz Atay\nSaatleri Ayarlama Enstitüsü|Ahmet Hamdi Tanpınar\n"
     "İnce Memed|Yaşar Kemal\nSinekli Bakkal|Halide Edib Adıvar\nYaban|Yakup Kadri Karaosmanoğlu\nAşk-ı Memnu|Halit Ziya Uşaklıgil\n"
     "Mai ve Siyah|Halit Ziya Uşaklıgil\nAraba Sevdası|Recaizade Mahmut Ekrem\nİntibah|Namık Kemal\nŞair Evlenmesi|Şinasi\n"
     "Safahat|Mehmet Akif Ersoy\nHuzur|Ahmet Hamdi Tanpınar\nKürk Mantolu Madonna|Sabahattin Ali"},
};
static const int BUILTIN_N = sizeof(BUILTIN_DECKS) / sizeof(BUILTIN_DECKS[0]);

struct Card {
  String front, back;
};
struct DeckInfo {
  String name, path;  // path boşsa yerleşik (index)
  int builtin = -1;
};
static std::vector<DeckInfo> kdDecks;
static std::vector<Card> kdCards;
static std::vector<uint8_t> kdBox;
static int kdDeck = -1, kdCur = -1, kdFirst = 0, kdSeen = 0;
static bool kdFlip = false;
static const int KD_ROW = 36, KD_ROWS = 5;

static void kdParse(const String& data, std::vector<Card>& out) {
  int start = 0;
  while (start < (int)data.length()) {
    int nl = data.indexOf('\n', start);
    if (nl < 0) nl = data.length();
    String line = data.substring(start, nl);
    start = nl + 1;
    line.trim();
    if (!line.length() || line.startsWith("#")) continue;
    int bar = line.lastIndexOf('|');  // önde "|x|" gibi ifadeler olabilir
    if (bar < 0) bar = line.indexOf('\t');
    if (bar <= 0) continue;
    Card c;
    c.front = line.substring(0, bar);
    c.back = line.substring(bar + 1);
    c.front.trim();
    c.back.trim();
    if (c.front.length() && out.size() < 250) out.push_back(c);
  }
}

static String kdKey() {
  const DeckInfo& d = kdDecks[kdDeck];
  if (d.builtin >= 0) return "b" + String(d.builtin);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < d.path.length(); i++) h = (h ^ (uint8_t)d.path[i]) * 16777619u;
  char k[12];
  snprintf(k, sizeof k, "s%08x", (unsigned)h);
  return k;
}

static void kdSaveBoxes() {
  Lock l;
  prefs.begin("kart", false);
  prefs.putBytes(kdKey().c_str(), kdBox.data(), kdBox.size());
  prefs.end();
}

static int kdLearned() {
  int n = 0;
  for (auto b : kdBox)
    if (b >= 3) n++;
  return n;
}

static void kdLoadDecks() {
  kdDecks.clear();
  for (int i = 0; i < BUILTIN_N; i++) {
    DeckInfo d;
    d.name = BUILTIN_DECKS[i].name;
    d.builtin = i;
    kdDecks.push_back(d);
  }
  if (!sdOk) return;
  if (!SD.exists(CARDS_DIR)) {
    if (!SD.exists("/daxen")) SD.mkdir("/daxen");
    SD.mkdir(CARDS_DIR);
    File f = SD.open(CARDS_DIR "/ornek.txt", FILE_WRITE);  // kendi desteni yazmak için örnek
    if (f) {
      f.print("# Her satır bir kart: ön | arka\n# Bu dosyayı kopyalayıp kendi desteni yap.\nTürkiye'nin başkenti | Ankara\n"
              "Su kaç derecede kaynar (deniz seviyesi)? | 100 °C\n");
      f.close();
    }
  }
  File d = SD.open(CARDS_DIR);
  for (File f = d.openNextFile(); f && kdDecks.size() < 60; f = d.openNextFile()) {
    String n = baseName(f.name());
    if (!f.isDirectory() && extOf(n) == "txt") {
      DeckInfo di;
      di.name = n.substring(0, n.lastIndexOf('.'));
      di.path = joinPath(CARDS_DIR, n);
      kdDecks.push_back(di);
    }
    f.close();
  }
  d.close();
}

static void kdNext() {
  int n = kdCards.size();
  if (!n) return;
  // ağırlık: düşük kutudaki kartlar daha sık
  uint32_t total = 0;
  for (int i = 0; i < n; i++)
    if (i != kdCur || n == 1) total += (6 - kdBox[i]) * (6 - kdBox[i]);
  uint32_t r = rnd(total), acc = 0;
  for (int i = 0; i < n; i++) {
    if (i == kdCur && n > 1) continue;
    acc += (6 - kdBox[i]) * (6 - kdBox[i]);
    if (r < acc) {
      kdCur = i;
      break;
    }
  }
  kdFlip = false;
}

static void kdOpenDeck(int i) {
  kdDeck = i;
  kdCards.clear();
  const DeckInfo& d = kdDecks[i];
  if (d.builtin >= 0) {
    kdParse(BUILTIN_DECKS[d.builtin].data, kdCards);
  } else {
    File f = SD.open(d.path);
    if (f) {
      String s = f.readString();
      f.close();
      kdParse(s, kdCards);
    }
  }
  kdBox.assign(kdCards.size(), 0);
  {
    Lock l;
    prefs.begin("kart", true);
    prefs.getBytes(kdKey().c_str(), kdBox.data(), kdBox.size());
    prefs.end();
  }
  for (auto& b : kdBox) b = std::min<uint8_t>(b, 5);
  kdCur = -1;
  kdSeen = 0;
  kdNext();
}

static void kdDrawCard() {
  fillBg(0, HDR_H, SCR_W, SCR_H - HDR_H);
  if (kdCards.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_CARDS, "Destede kart yok", "Her satıra \"ön | arka\" yaz.");
    return;
  }
  const Card& c = kdCards[kdCur];
  panel(12, HDR_H + 8, 296, 150, C_SURF, kdFlip ? C_ACC : C_LINE);
  auto fl = wrapText(c.front, F_MD, 272, 3);
  int y = HDR_H + 22 + (kdFlip ? 0 : (3 - (int)fl.size()) * 11 + 18);
  for (auto& l : fl) {
    text(l, 160, y, F_MD, C_TEXT, C_SURF, TC_DATUM);
    y += 22;
  }
  if (kdFlip) {
    tft.drawFastHLine(40, HDR_H + 92, 240, C_LINE);
    auto bl = wrapText(c.back, F_MD, 272, 2);
    int by = HDR_H + 104;
    for (auto& l : bl) {
      text(l, 160, by, F_MD, C_ACC, C_SURF, TC_DATUM);
      by += 22;
    }
  }
  // kutu göstergesi
  for (int k = 0; k < 5; k++) tft.fillRect(130 + k * 13, HDR_H + 146, 9, 3, k < kdBox[kdCur] ? C_ACC : C_LINE);
  if (!kdFlip) {
    button(12, 196, 296, 38, "Cevabı göster", B_PRIMARY);
  } else {
    button(12, 196, 144, 38, "Bilmiyorum", B_NORMAL, I_CLOSE);
    button(164, 196, 144, 38, "Biliyorum", B_PRIMARY, I_CHECK);
  }
}

static void kdDrawList() {
  int n = kdDecks.size();
  kdFirst = constrain(kdFirst, 0, std::max(0, n - KD_ROWS));
  for (int r = 0; r < KD_ROWS + 1; r++) {
    int i = kdFirst + r, y = HDR_H + r * KD_ROW;
    int h = std::min(KD_ROW, SCR_H - y);
    fillBg(0, y, SCR_W, h);
    if (r >= KD_ROWS || i >= n) continue;
    listRow(y, KD_ROW, kdDecks[i].builtin >= 0 ? I_CARDS : I_FILE, kdDecks[i].name,
            kdDecks[i].builtin >= 0 ? String("Yerleşik deste") : String("SD kart"), "", true);
  }
  scrollBar(HDR_H, KD_ROW * KD_ROWS, kdFirst, KD_ROWS, n);
}

static void kdDraw(bool full) {
  if (kdDeck >= 0) {
    if (full) {
      header(kdDecks[kdDeck].name);
      text(String(kdLearned()) + "/" + String(kdCards.size()), 262, 15, F_XS, C_TEXT2, C_BG, MR_DATUM);
    }
    kdDrawCard();
    return;
  }
  if (full) header("Bilgi kartları");
  kdDrawList();
}

static void kdTap(int x, int y) {
  if (kdDeck < 0) {
    int r = (y - HDR_H) / KD_ROW, i = kdFirst + r;
    if (y >= HDR_H && r < KD_ROWS && i < (int)kdDecks.size()) {
      kdOpenDeck(i);
      kdDraw(true);
    }
    return;
  }
  if (kdCards.empty() || y < 190) {
    if (!kdFlip && y >= HDR_H && y < 190 && !kdCards.empty()) {  // karta dokununca da çevrilir
      kdFlip = true;
      kdDrawCard();
    }
    return;
  }
  if (!kdFlip) {
    kdFlip = true;
    kdDrawCard();
    return;
  }
  uint8_t& b = kdBox[kdCur];
  if (x >= 160) b = std::min<uint8_t>(5, b + 1);
  else b = 0;
  kdSeen++;
  kdSaveBoxes();
  kdNext();
  kdDraw(true);
}

static void kdSwipe(int dir) {
  if (kdDeck < 0 && (dir == SW_UP || dir == SW_DOWN)) {
    int n = kdDecks.size();
    kdFirst = constrain(kdFirst + (dir == SW_UP ? KD_ROWS - 1 : -(KD_ROWS - 1)), 0, std::max(0, n - KD_ROWS));
    kdDrawList();
  } else if (kdDeck >= 0 && dir == SW_LEFT) {
    kdNext();
    kdDrawCard();
  }
}

static bool kdBack() {
  if (kdDeck >= 0) {
    kdDeck = -1;
    kdCards.clear();
    kdDraw(true);
    return true;
  }
  return false;
}

static void kdOpen() {
  kdDeck = -1;
  kdLoadDecks();
}

// Kapanınca deste bellekten çıkar (ilerleme her değişimde NVS'ye yazılıyor: kdSaveBoxes)
static void kdClose() {
  std::vector<Card>().swap(kdCards);
  std::vector<uint8_t>().swap(kdBox);
  std::vector<DeckInfo>().swap(kdDecks);
  kdDeck = -1;
}

static const App APP_CARDS = {"Bilgi kartları", "Kartlar", I_CARDS, kdOpen, kdDraw, kdTap, nullptr, kdSwipe, nullptr, nullptr, kdBack, kdClose};
