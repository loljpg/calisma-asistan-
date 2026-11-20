#pragma once
// Yapılacaklar: basit liste. Kutuya dokun: işaretle; yazıya dokun: düzenle; basılı tut: sil, taşı.
// Kalıcı bellekte (NVS) saklanır, SD kart gerekmez.
#include "../os/shell.h"

struct TodoItem {
  String text;
  bool done;
  TodoItem(const String& t = String(), bool d = false) : text(t), done(d) {}
};
static std::vector<TodoItem> todos;
static bool todoLoaded = false;
static int todoFirst = 0;
static const int TD_ROW = 34, TD_ROWS = 6, TD_MAX = 40;

static void todoSave() {
  String s;
  for (auto& t : todos) {
    s += t.done ? "1" : "0";
    s += t.text;
    s += '\n';
  }
  Lock l;
  prefs.begin("todo", false);
  if (s.length()) prefs.putBytes("l", s.c_str(), s.length());
  else prefs.remove("l");
  prefs.end();
}

static void todoLoad() {
  todos.clear();
  String s;
  {
    Lock l;
    prefs.begin("todo", true);
    size_t n = prefs.getBytesLength("l");
    if (n) {
      std::unique_ptr<char[]> buf(new char[n + 1]);
      prefs.getBytes("l", buf.get(), n);
      buf[n] = 0;
      s = buf.get();
    }
    prefs.end();
  }
  int start = 0;
  while (start < (int)s.length()) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) nl = s.length();
    String line = s.substring(start, nl);
    if (line.length() >= 1) todos.push_back({line.substring(1), line[0] == '1'});
    start = nl + 1;
  }
  todoLoaded = true;
}

static void tdDrawList() {
  if (todos.empty()) {
    emptyState(HDR_H, SCR_H - HDR_H, I_TODO, "Liste boş", "Sağ üstteki + ile ekle.");
    return;
  }
  int n = todos.size();
  todoFirst = constrain(todoFirst, 0, std::max(0, n - TD_ROWS));
  for (int r = 0; r < TD_ROWS; r++) {
    int y = HDR_H + r * TD_ROW, i = todoFirst + r;
    fillBg(0, y, SCR_W, TD_ROW);
    if (i >= n) continue;
    const TodoItem& t = todos[i];
    checkBox(12, y + 7, 20, t.done, C_BG);
    String s = fit(t.text, F_SM, 262);
    text(s, 44, y + TD_ROW / 2 + 1, F_SM, t.done ? C_TEXT3 : C_TEXT, C_BG, ML_DATUM);
    if (t.done) tft.drawFastHLine(44, y + TD_ROW / 2 + 1, textW(s, F_SM), C_TEXT3);
    hline(44, y + TD_ROW - 1, 264);
  }
  int left = 0;
  for (auto& t : todos)
    if (!t.done) left++;
  fillBg(0, HDR_H + TD_ROW * TD_ROWS, SCR_W, SCR_H - HDR_H - TD_ROW * TD_ROWS);
  text(String(left) + " iş kaldı · " + String(n - left) + " tamam", 12, SCR_H - 13, F_XS, C_TEXT3, C_BG, ML_DATUM);
  scrollBar(HDR_H, TD_ROW * TD_ROWS, todoFirst, TD_ROWS, n);
}

static void tdDraw(bool full) {
  if (!todoLoaded) todoLoad();
  if (full) {
    header("Yapılacaklar");
    headerAction(0, I_PLUS);
    bool anyDone = false;
    for (auto& t : todos) anyDone |= t.done;
    if (anyDone) headerAction(1, I_TRASH);
  }
  tdDrawList();
}

static void tdTap(int x, int y) {
  if (y < HDR_H) {
    int h = headerHit(x, y);
    if (h == 0) {
      if ((int)todos.size() >= TD_MAX) {
        toast("Liste dolu (en fazla " + String(TD_MAX) + ")");
        return;
      }
      textInput("Yeni iş", "", [](const String& v) {
        String t = v;
        t.trim();
        t.replace("\n", " ");
        if (!t.length()) return;
        todos.push_back({t, false});
        todoFirst = std::max(0, (int)todos.size() - TD_ROWS);
        todoSave();
      }, false, 80, "Ne yapılacak?");
    } else if (h == 1) {
      dialog("Tamamlananlar silinsin mi?", "İşaretli işler listeden kalkar.", {"Vazgeç", "Sil"}, [](int c) {
        if (c != 1) return;
        todos.erase(std::remove_if(todos.begin(), todos.end(), [](const TodoItem& t) { return t.done; }), todos.end());
        todoSave();
      });
    }
    return;
  }
  int r = (y - HDR_H) / TD_ROW, i = todoFirst + r;
  if (r >= TD_ROWS || i >= (int)todos.size()) return;
  if (x < 44) {
    todos[i].done = !todos[i].done;
    todoSave();
    tdDraw(true);
  } else {
    textInput("İşi düzenle", todos[i].text, [i](const String& v) {
      String t = v;
      t.trim();
      if (i < (int)todos.size() && t.length()) todos[i].text = t;
      todoSave();
    }, false, 80);
  }
}

static bool tdHold(int x, int y) {
  int r = (y - HDR_H) / TD_ROW, i = todoFirst + r;
  if (y < HDR_H || r >= TD_ROWS || i >= (int)todos.size()) return false;
  menu(todos[i].text, {"Sil", "En üste taşı", "En alta taşı"}, [i](int c) {
    if (i >= (int)todos.size()) return;
    TodoItem t = todos[i];
    if (c == 0) todos.erase(todos.begin() + i);
    else if (c == 1) {
      todos.erase(todos.begin() + i);
      todos.insert(todos.begin(), t);
    } else if (c == 2) {
      todos.erase(todos.begin() + i);
      todos.push_back(t);
    } else return;
    todoSave();
    tdDraw(true);
  });
  return true;
}

static void tdSwipe(int dir) {
  if (dir != SW_UP && dir != SW_DOWN) return;
  int n = todos.size();
  int nf = constrain(todoFirst + (dir == SW_UP ? TD_ROWS - 1 : -(TD_ROWS - 1)), 0, std::max(0, n - TD_ROWS));
  if (nf != todoFirst) {
    todoFirst = nf;
    tdDrawList();
  }
}

static const App APP_TODO = {"Yapılacaklar", "Yapılacaklar", I_TODO, nullptr, tdDraw, tdTap, tdHold, tdSwipe, nullptr, nullptr, nullptr, nullptr};
