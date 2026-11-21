#pragma once
// Komut yorumlayıcısı: satırı parçalara ayırır, izin listesindeki (komut tablosu) karşılığını bulur,
// argüman sayısını ve yetki seviyesini denetler, işleyiciyi çağırır. Tabloda olmayan hiçbir şey
// çalışmaz; girdi asla fonksiyon adı, adres ya da serbest dosya yolu olarak kullanılmaz.
// Komutların kendisi apps/terminal.h'de; aynı yorumlayıcı ekrandaki Terminal'den ve USB seri
// porttan (115200) kullanılır. Bu bir Linux kabuğu değildir.
#include <vector>

#define CMD_LINE_MAX 96
#define CMD_ARGS_MAX 6

enum CmdLevel : uint8_t { CL_USER = 0, CL_DEV = 1 };  // geliştirici seviyesi Aşama 9'da PIN'le açılır

struct CmdOut {
  std::vector<String> lines;
  bool clear = false;  // ekranı temizle
  void add(const String& s) { lines.push_back(s); }
  void kv(const String& k, const String& v) { lines.push_back(k + ": " + v); }
};

typedef void (*CmdFn)(const std::vector<String>& a, CmdOut& o);

struct Cmd {
  const char* name;  // ilk kelime
  const char* sub;   // ikinci kelime (yoksa nullptr)
  uint8_t level, minA, maxA;
  CmdFn fn;
  const char* args;  // yardımda gösterilen argümanlar
  const char* help;  // tek satır
};

// Satırı kelimelere ayırır; çift tırnak içi tek kelime. Hata varsa false ve sebebi err'de.
static bool cmdSplit(const String& line, std::vector<String>& out, String& err) {
  out.clear();
  if (line.length() > CMD_LINE_MAX) {
    err = "Satır çok uzun (en fazla " + String(CMD_LINE_MAX) + " karakter)";
    return false;
  }
  String cur;
  bool q = false, any = false;
  for (size_t i = 0; i < line.length(); i++) {
    char c = line[i];
    if (c == '"') {
      q = !q;
      any = true;
    } else if ((c == ' ' || c == '\t') && !q) {
      if (any) out.push_back(cur);
      cur = "";
      any = false;
    } else if ((uint8_t)c < 32) {
      err = "Satırda denetim karakteri var";
      return false;
    } else {
      cur += c;
      any = true;
    }
  }
  if (q) {
    err = "Tırnak kapanmamış";
    return false;
  }
  if (any) out.push_back(cur);
  if (out.size() > 2 + CMD_ARGS_MAX) {
    err = "Çok fazla kelime";
    return false;
  }
  return true;
}

// Satırı çalıştırır. tbl/n: komut tablosu; level: çağıranın yetkisi.
static void cmdRun(const Cmd* tbl, size_t n, const String& line, uint8_t level, CmdOut& o) {
  std::vector<String> w;
  String err;
  if (!cmdSplit(line, w, err)) {
    o.add("Hata: " + err);
    return;
  }
  if (w.empty()) return;
  String name = w[0];
  name.toLowerCase();
  const Cmd* hit = nullptr;
  bool nameKnown = false;
  for (size_t i = 0; i < n && !hit; i++) {
    if (name != tbl[i].name) continue;
    nameKnown = true;
    if (!tbl[i].sub) hit = &tbl[i];
    else if (w.size() > 1) {
      String s = w[1];
      s.toLowerCase();
      if (s == tbl[i].sub) hit = &tbl[i];
    }
  }
  if (!hit) {
    if (nameKnown) {
      o.add(name + ": alt komut gerekli ya da tanınmıyor. Seçenekler:");
      for (size_t i = 0; i < n; i++)
        if (name == tbl[i].name && tbl[i].sub) o.add("  " + name + " " + tbl[i].sub + (tbl[i].args[0] ? String(" ") + tbl[i].args : String("")));
    } else {
      o.add("Bilinmeyen komut: " + name + " (komutlar için: help)");
    }
    return;
  }
  std::vector<String> a(w.begin() + (hit->sub ? 2 : 1), w.end());
  if (a.size() < hit->minA || a.size() > hit->maxA) {
    o.add("Kullanım: " + String(hit->name) + (hit->sub ? String(" ") + hit->sub : String("")) +
          (hit->args[0] ? String(" ") + hit->args : String("")));
    return;
  }
  if (hit->level > level) {
    o.add("Bu komut geliştirici yetkisi ister (açmak için: dev)");
    return;
  }
  hit->fn(a, o);
}

// Komut geçmişi: son 16 satır, aynı satır art arda tekrar edilmez
struct CmdHistory {
  std::vector<String> items;
  void push(const String& s) {
    if (!s.length() || (items.size() && items.back() == s)) return;
    items.push_back(s);
    if (items.size() > 16) items.erase(items.begin());
  }
};
