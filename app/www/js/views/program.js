// Program ekranı: haftalık ödevler ve çalışma saatleri.
import * as L from '../logic.js';
import { icon, esc, options, pageHead, openSheet, closeSheet, sheetHead, toast } from '../ui.js';

let tab = 'tasks';
const DAY_ORDER = [1, 2, 3, 4, 5, 6, 0];

export function render(app) {
  const right = tab === 'tasks' ? `<button class="icon-btn glass" data-action="program:add" aria-label="Ödev ekle">${icon('plus')}</button>` : '';
  return `
  ${pageHead('Program', 'Ödevler ve çalışma saatleri', right)}
  <div class="seg glass" role="group" aria-label="Bölüm">
    <button aria-pressed="${tab === 'tasks'}" data-action="program:tab" data-tab="tasks">Ödevler</button>
    <button aria-pressed="${tab === 'hours'}" data-action="program:tab" data-tab="hours">Çalışma saatleri</button>
  </div>
  ${tab === 'tasks' ? tasksView(app) : hoursView(app)}`;
}

function tasksView(app) {
  const s = app.s;
  if (!s.tasks.length) {
    return `
    <div class="empty glass">
      <b>Henüz ödev yok</b>
      <p>Haftalık ödevlerini ekle. Claude plan yaparken son günü yaklaşanları öne alır ve kalan süreyi günlere dağıtır.</p>
      <button class="btn primary" data-action="program:add">${icon('plus', 16)} Ödev ekle</button>
    </div>`;
  }
  const active = s.tasks.filter((t) => !t.done).sort((a, b) => ((a.due || '9999') < (b.due || '9999') ? -1 : 1));
  const done = s.tasks.filter((t) => t.done).slice(-10).reverse();
  return `
  ${active.length ? `<div class="list">${active.map((t) => taskCard(t, app)).join('')}</div>` : '<p class="muted small">Aktif ödevin kalmadı.</p>'}
  ${done.length ? `<h2 class="set-title">Tamamlananlar</h2><div class="list">${done.map((t) => taskCard(t, app)).join('')}</div>` : ''}`;
}

function dueInfo(t, today) {
  if (!t.due) return { txt: '', late: false };
  const n = L.daysBetween(today, t.due);
  if (n > 1) return { txt: `${L.shortDate(t.due)} · ${n} gün kaldı`, late: false };
  if (n === 1) return { txt: 'Son gün yarın', late: true };
  if (n === 0) return { txt: 'Son gün bugün', late: true };
  return { txt: `${-n} gün gecikti`, late: true };
}

function taskCard(t, app) {
  const spent = L.taskSpent(app.s.sessions, t.id);
  const pct = t.dk ? Math.min(100, Math.round((spent / t.dk) * 100)) : 0;
  const due = dueInfo(t, app.today);
  const right = t.done ? '<span class="due">Tamamlandı</span>' : due.txt ? `<span class="due${due.late ? ' late' : ''}">${due.txt}</span>` : '';
  const progress = t.dk
    ? `<div class="progress thin"><i style="width:${pct}%"></i></div><span class="muted small">${spent} / ${t.dk} dk</span>`
    : spent ? `<span class="muted small">${spent} dk çalışıldı</span>` : '';
  return `
  <button class="task glass" data-action="program:edit" data-id="${t.id}">
    <div class="task-top"><span class="chip"><span class="dot" style="background:${L.dersRenk(t.ders)}"></span>${esc(t.ders)}</span>${right}</div>
    <b>${esc(t.konu)}</b>
    ${t.not ? `<span class="muted small">${esc(t.not)}</span>` : ''}
    ${progress}
  </button>`;
}

function hoursView(app) {
  const s = app.s;
  return `
  <form class="form" data-form="program:hours">
    <div class="card glass">
      ${DAY_ORDER.map((d) => {
        const h = s.hours[d];
        const g = L.GUNLER[d];
        return `<div class="hours-row">
          <div class="spread">
            <span>${g}</span>
            <input class="toggle" type="checkbox" name="on_${d}" ${h.on ? 'checked' : ''} aria-label="${g} çalışacağım">
          </div>
          <div class="grid2">
            <input class="input" type="time" name="start_${d}" value="${h.start}" aria-label="${g} başlangıç">
            <input class="input" type="time" name="end_${d}" value="${h.end}" aria-label="${g} bitiş">
          </div>
        </div>`;
      }).join('')}
    </div>
    <label class="field">Günlük hedef (dakika)<input class="input" type="number" inputmode="numeric" name="goal" min="30" max="900" step="15" value="${s.settings.dailyGoalMin}"></label>
    <p class="hint">Claude oturumları sadece bu saat aralıklarına yerleştirir. Kapalı günlere sen istemedikçe plan yapmaz.</p>
    <button class="btn primary block" type="submit">Kaydet</button>
  </form>`;
}

function openTaskSheet(app, id) {
  const t = id ? app.s.tasks.find((x) => x.id === id) : null;
  const def = t || { ders: 'Matematik', konu: '', not: '', dk: '', due: L.addDays(app.today, 3) };
  const list = L.DERSLER.includes(def.ders) ? L.DERSLER : [def.ders, ...L.DERSLER];
  openSheet(
    `${sheetHead(t ? 'Ödevi düzenle' : 'Ödev ekle')}
    <form class="form" data-form="program:save" data-id="${id || ''}">
      <label class="field">Ders<select class="input" name="ders">${options(list, def.ders)}</select></label>
      <label class="field">Konu / ödev<input class="input" name="konu" required maxlength="120" value="${esc(def.konu)}" placeholder="Örn: Limit – 3D yayınları 2. test"></label>
      <label class="field">Açıklama (isteğe bağlı)<input class="input" name="not" maxlength="200" value="${esc(def.not || '')}" placeholder="Örn: Hocaya cuma teslim"></label>
      <div class="grid2">
        <label class="field">Tahmini süre (dk)<input class="input" type="number" inputmode="numeric" name="dk" min="10" max="2000" step="5" value="${def.dk || ''}" placeholder="90"></label>
        <label class="field">Son gün<input class="input" type="date" name="due" value="${def.due || ''}"></label>
      </div>
      <button class="btn primary block" type="submit">${t ? 'Kaydet' : 'Ekle'}</button>
      ${t ? `<div class="btn-row">
        <button class="btn glass" type="button" data-action="program:toggleDone" data-id="${t.id}">${icon(t.done ? 'undo' : 'check', 16)} ${t.done ? 'Geri al' : 'Tamamlandı'}</button>
        <button class="btn danger" type="button" data-action="program:remove" data-id="${t.id}">${icon('trash', 16)} Sil</button>
      </div>` : ''}
    </form>`,
    'Ödev'
  );
}

export const actions = {
  tab(el, app) {
    tab = el.dataset.tab;
    app.render();
  },
  add(el, app) {
    openTaskSheet(app, null);
  },
  edit(el, app) {
    openTaskSheet(app, el.dataset.id);
  },
  toggleDone(el, app) {
    let done = false;
    app.update((s) => {
      const t = s.tasks.find((x) => x.id === el.dataset.id);
      if (t) {
        t.done = !t.done;
        done = t.done;
      }
    });
    closeSheet();
    toast(done ? 'Ödev tamamlandı.' : 'Ödev yeniden aktif.');
  },
  remove(el, app) {
    app.update((s) => {
      s.tasks = s.tasks.filter((x) => x.id !== el.dataset.id);
    });
    closeSheet();
    toast('Ödev silindi.');
  }
};

export const forms = {
  save(data, form, app) {
    const id = form.dataset.id;
    const konu = (data.konu || '').trim();
    if (!konu) throw new Error('Ödevin konusunu yaz.');
    const dk = data.dk ? Math.round(Number(data.dk)) : null;
    if (dk !== null && !(dk >= 10 && dk <= 2000)) throw new Error('Tahmini süre 10 ile 2000 dakika arasında olmalı.');
    const fields = { ders: data.ders, konu, not: (data.not || '').trim(), dk, due: data.due || null };
    app.update((s) => {
      if (id) {
        const t = s.tasks.find((x) => x.id === id);
        if (t) Object.assign(t, fields);
      } else {
        s.tasks.push({ id: L.uid(), ...fields, done: false, created: Date.now() });
      }
    });
    closeSheet();
    toast(id ? 'Ödev güncellendi.' : 'Ödev eklendi.');
  },
  hours(data, form, app) {
    const goal = Math.round(Number(data.goal));
    if (!(goal >= 30 && goal <= 900)) throw new Error('Günlük hedef 30 ile 900 dakika arasında olmalı.');
    for (const d of DAY_ORDER) {
      if (data[`on_${d}`] && L.toMin(data[`start_${d}`]) >= L.toMin(data[`end_${d}`])) {
        throw new Error(`${L.GUNLER[d]}: bitiş saati başlangıçtan sonra olmalı.`);
      }
    }
    app.update((s) => {
      for (const d of DAY_ORDER) {
        s.hours[d] = { on: !!data[`on_${d}`], start: data[`start_${d}`], end: data[`end_${d}`] };
      }
      s.settings.dailyGoalMin = goal;
    });
    toast('Çalışma saatleri kaydedildi.');
  }
};
