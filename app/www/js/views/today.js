// Bugün ekranı: özet, kronometre, günün planı, Claude ile planlama.
import * as L from '../logic.js';
import { icon, esc, options, radio, openSheet, setSheet, closeSheet, sheetHead, toast, copyText } from '../ui.js';
import { askClaude, MODELS } from '../ai.js';
import { isSignedIn, syncState } from '../sync.js';
import { bt } from '../bt.js';

let dayOffset = 0; // 0: bugün, 1: yarın
let pending = null; // Claude'dan gelen, henüz uygulanmamış plan

const viewDate = (app) => L.addDays(app.today, dayOffset);

// ---------- Görünüm ----------
export function render(app) {
  const s = app.s;
  const today = app.today;
  const date = viewDate(app);
  const plan = s.plans[date];
  const items = plan ? plan.items.filter((i) => i.durum !== 'tasindi') : [];
  const todayItems = (s.plans[today]?.items || []).filter((i) => i.durum !== 'tasindi');
  const studied = L.minutesOn(s.sessions, today);
  const goal = Number(s.settings.dailyGoalMin) || 300;
  const pct = Math.min(100, Math.round((studied / goal) * 100));
  const doneN = todayItems.filter((i) => i.durum === 'tamam').length;
  const cd = L.examCountdown(s.settings.examDate, today);
  const t = s.timer;
  const nowMin = date === today ? L.toMin(L.timeStr()) : -1;

  return `
  <header class="page-head">
    <div><span class="sub">${L.longDate(today)}</span><h1>Bugün</h1></div>
    <button class="icon-btn surface" data-action="ui:go" data-route="settings" aria-label="Ayarlar">${icon('settings')}</button>
  </header>
  ${app.updateInfo?.newer ? updateBanner(app.updateInfo) : ''}
  <section class="card surface" aria-label="Bugünün özeti">
    <div class="spread">
      <span class="eyebrow">Bugün çalışılan</span>
      <span class="chip">${icon('flame', 14)} ${L.streak(s.sessions, today)} gün seri</span>
    </div>
    <div class="big-row"><span class="big">${L.fmtMin(studied)}</span><span class="muted">/ ${L.fmtMin(goal)} hedef</span></div>
    <div class="progress" role="progressbar" aria-valuenow="${pct}" aria-valuemin="0" aria-valuemax="100" aria-label="Günlük hedef"><i style="width:${pct}%"></i></div>
    <div class="chips">
      ${cd === null ? '<button class="chip" style="border:none" data-action="ui:go" data-route="settings">Sınav tarihini ayarla</button>' : `<span class="chip">Sınava ${cd} gün</span>`}
      <span class="chip">${doneN}/${todayItems.length} oturum</span>
      ${bt.state?.authed || (isSignedIn() && L.deviceOnline(syncState.status)) ? '<span class="chip ok"><span class="led"></span>Masa saati bağlı</span>' : ''}
    </div>
  </section>
  ${t ? timerCard(t) : ''}
  <div class="section-head">
    <h2>${dayOffset ? 'Yarının planı' : 'Bugünün planı'}</h2>
    <button class="btn surface" data-action="today:aiSheet">${icon('sparkles', 16)} ${items.length ? 'Düzenle' : 'Planla'}</button>
  </div>
  <div class="seg small surface" role="group" aria-label="Gün seçimi">
    <button aria-pressed="${dayOffset === 0}" data-action="today:day" data-off="0">Bugün</button>
    <button aria-pressed="${dayOffset === 1}" data-action="today:day" data-off="1">Yarın</button>
  </div>
  ${items.length ? `<div class="list">${items.map((i) => row(i, date, t, nowMin)).join('')}</div>` : emptyState(date === today)}
  ${plan?.not ? `<div class="note surface">${icon('sparkles', 18)}<p>${esc(plan.not)}</p></div>` : ''}
  <div class="btn-row">
    <button class="btn surface" data-action="today:addItem">${icon('plus', 16)} Oturum ekle</button>
    ${t ? '' : `<button class="btn surface" data-action="today:freeTimer">${icon('timer', 16)} Kronometre</button>`}
  </div>`;
}

function updateBanner(info) {
  return `<div class="banner surface">${icon('download', 20)}<p>Yeni sürüm hazır (v${info.latest})</p><a class="btn primary" href="${esc(info.url)}">İndir</a></div>`;
}

function timerCard(t) {
  const running = !!t.startedAt;
  return `
  <section class="card ink" aria-label="Kronometre">
    <span class="eyebrow">${running ? 'Kronometre çalışıyor' : 'Kronometre duraklatıldı'}${bt.state?.authed || (isSignedIn() && L.deviceOnline(syncState.status)) ? ' · masa saatinde de' : ''}</span>
    <div class="timer-row">
      <div><b>${esc(t.ders)}</b><span class="muted small">${esc(t.konu || 'Serbest çalışma')}</span></div>
      <span class="timer-big" data-timer>${L.fmtClock(L.timerElapsedMs(t) / 1000)}</span>
    </div>
    <div class="btn-row">
      <button class="btn surface" data-action="today:timerToggle">${icon(running ? 'pause' : 'play', 16)} ${running ? 'Duraklat' : 'Devam et'}</button>
      <button class="btn primary" data-action="today:timerFinish">${icon('stop', 16)} Bitir</button>
      <button class="icon-btn surface" data-action="today:timerCancel" aria-label="Kronometreyi iptal et">${icon('x', 18)}</button>
    </div>
  </section>`;
}

function row(i, date, t, nowMin) {
  const done = i.durum === 'tamam';
  const active = !!t && t.itemId === i.id;
  const late = !done && !active && nowMin >= 0 && L.toMin(i.saat) + i.dk < nowMin;
  const tag = i.durum === 'ertelendi' ? '<span class="tag">Ertelendi</span>' : i.fromPrev ? '<span class="tag blue">Taşındı</span>' : '';
  const sub = active
    ? `<span class="row-sub running">${t.startedAt ? 'Devam ediyor' : 'Duraklatıldı'} · <span data-timer>${L.fmtClock(L.timerElapsedMs(t) / 1000)}</span></span>`
    : `<span class="row-sub">${esc(i.konu || '—')}</span>`;
  return `
  <div class="row surface${done ? ' is-done' : ''}${active ? ' is-active' : ''}${late ? ' is-late' : ''}">
    <button class="row-main" data-action="today:itemSheet" data-id="${i.id}" data-date="${date}">
      <div class="row-time"><b>${i.saat}</b><span>${i.dk} dk</span></div>
      <div class="row-body">
        <div class="row-title"><span class="dot" style="background:${L.dersRenk(i.ders)}"></span><span class="t">${esc(i.ders)}</span>${tag}</div>
        ${sub}
      </div>
    </button>
    <button class="check${done ? ' on' : ''}" data-action="today:toggle" data-id="${i.id}" data-date="${date}" aria-label="${esc(i.ders)}: ${done ? 'tamamlandı, geri al' : 'tamamlandı olarak işaretle'}"><span>${icon('check', 16, 3)}</span></button>
  </div>`;
}

function emptyState(isToday) {
  return `
  <div class="empty surface">
    <b>${isToday ? 'Bugün için plan yok' : 'Yarın için plan yok'}</b>
    <p>Claude ödevlerine, yarım kalan oturumlarına ve deneme sonuçlarına bakarak senin için bir plan hazırlasın ya da oturumları elle ekle.</p>
    <button class="btn primary" data-action="today:aiSheet">${icon('sparkles', 16)} Claude ile planla</button>
  </div>`;
}

// ---------- Yardımcılar ----------
function finishTimer(s) {
  const t = s.timer;
  if (!t) return 0;
  const sec = Math.round(L.timerElapsedMs(t) / 1000);
  s.timer = null;
  if (sec < 60) return 0;
  s.sessions.push({
    id: L.uid(), tarih: L.dateStr(), ders: t.ders, konu: t.konu, sec,
    itemId: t.itemId || null, taskId: t.taskId || null, kaynak: 'kronometre', end: Date.now()
  });
  if (t.itemId) {
    const it = L.findItem(s, t.date, t.itemId);
    if (it) it.durum = 'tamam';
  }
  return sec / 60;
}

function nextSlot() {
  const d = new Date();
  return `${L.pad((d.getHours() + 1) % 24)}:00`;
}

function taskOptions(s, selected) {
  const tasks = s.tasks.filter((t) => !t.done || t.id === selected);
  if (!tasks.length) return '';
  return `<label class="field">Ödev (isteğe bağlı)<select class="input" name="taskId"><option value="">Ödeve bağlı değil</option>${tasks
    .map((t) => `<option value="${t.id}"${t.id === selected ? ' selected' : ''}>${esc(t.ders)} · ${esc(t.konu)}</option>`)
    .join('')}</select></label>`;
}

function dersList(current) {
  return L.DERSLER.includes(current) ? L.DERSLER : [current, ...L.DERSLER];
}

function openItemSheet(app, date, id) {
  const s = app.s;
  const it = L.findItem(s, date, id);
  if (!it) return;
  const done = it.durum === 'tamam';
  const task = it.taskId ? s.tasks.find((t) => t.id === it.taskId) : null;
  const active = s.timer?.itemId === id;
  const ref = `data-id="${id}" data-date="${date}"`;
  const sub = `${it.saat} · ${it.dk} dk${date !== app.today ? ' · ' + L.longDate(date) : ''}`;
  openSheet(
    `${sheetHead(it.ders, sub)}
    <div class="form">
      ${it.konu ? `<p>${esc(it.konu)}</p>` : ''}
      ${task ? `<p class="hint">Ödev: ${esc(task.konu)}</p>` : ''}
      ${done
        ? `<button class="btn surface block" data-action="today:toggle" ${ref}>${icon('undo', 16)} Tamamlanmadı olarak işaretle</button>`
        : `${active || s.timer ? '' : `<button class="btn primary block" data-action="today:start" ${ref}>${icon('play', 16)} Kronometreyi başlat</button>`}
           <button class="btn surface block" data-action="today:toggle" ${ref}>${icon('check', 16)} Tamamlandı</button>
           ${it.durum === 'ertelendi' ? '' : `<button class="btn surface block" data-action="today:postpone" ${ref}>${icon('forward', 16)} Ertele</button>`}`}
      <div class="btn-row">
        <button class="btn surface" data-action="today:edit" ${ref}>${icon('edit', 16)} Düzenle</button>
        <button class="btn danger" data-action="today:remove" ${ref}>${icon('trash', 16)} Sil</button>
      </div>
    </div>`,
    it.ders
  );
}

function openEditSheet(app, date, id) {
  const s = app.s;
  const it = id ? L.findItem(s, date, id) : null;
  const def = it || { saat: nextSlot(), dk: 45, ders: 'Matematik', konu: '', taskId: null };
  openSheet(
    `${sheetHead(it ? 'Oturumu düzenle' : 'Oturum ekle', L.longDate(date))}
    <form class="form" data-form="today:saveItem" data-date="${date}" data-id="${id || ''}">
      <div class="grid2">
        <label class="field">Saat<input class="input" type="time" name="saat" value="${def.saat}" required></label>
        <label class="field">Süre (dk)<input class="input" type="number" inputmode="numeric" name="dk" min="5" max="240" step="5" value="${def.dk}" required></label>
      </div>
      <label class="field">Ders<select class="input" name="ders">${options(dersList(def.ders), def.ders)}</select></label>
      <label class="field">Konu<input class="input" name="konu" maxlength="120" value="${esc(def.konu)}" placeholder="Örn: Türev – 20 soru"></label>
      ${taskOptions(s, def.taskId)}
      <button class="btn primary block" type="submit">${it ? 'Kaydet' : 'Ekle'}</button>
    </form>`,
    'Oturum'
  );
}

function openAiSheet(app) {
  const st = app.s.settings;
  const manual = st.aiMode === 'manual' || !st.apiKey;
  const model = MODELS.find((m) => m.id === st.model)?.name || st.model;
  openSheet(
    `${sheetHead('Claude ile planla', manual ? 'Manuel mod' : model)}
    <form class="form" data-form="today:ai">
      <div class="field"><span>Hangi gün?</span><div class="radio-row">${radio('gun', '0', 'Bugün', dayOffset === 0)}${radio('gun', '1', 'Yarın', dayOffset === 1)}</div></div>
      <label class="field">İsteğin (isteğe bağlı)<textarea class="input" name="istek" maxlength="500" placeholder="Örn: 19:00'a kadar dersanedeyim, bugün matematiğe ağırlık ver."></textarea></label>
      ${st.aiMode === 'auto' && !st.apiKey ? '<p class="hint">API anahtarı girilmediği için manuel mod kullanılacak. Anahtarı Ayarlar’dan ekleyebilirsin.</p>' : ''}
      <p class="hint">Claude ödevlerini, yarım kalan oturumlarını, zayıf konularını ve son denemelerini dikkate alır. Tamamladığın oturumlar olduğu gibi kalır.</p>
      <button class="btn primary block" type="submit">${icon('sparkles', 16)} ${manual ? 'İstek metnini hazırla' : 'Planı oluştur'}</button>
    </form>`,
    'Claude ile planla'
  );
}

function showManual(date, text) {
  setSheet(
    `${sheetHead('Manuel mod', L.longDate(date))}
    <div class="form">
      <p class="hint">1. Aşağıdaki metni kopyala. 2. Claude uygulamasında yeni bir sohbete yapıştır. 3. Claude’un cevabının tamamını kopyalayıp alttaki kutuya yapıştır.</p>
      <textarea class="input mono" id="manual-prompt" readonly aria-label="Claude için istek metni">${esc(text)}</textarea>
      <button class="btn surface block" data-action="today:copyPrompt">${icon('copy', 16)} Metni kopyala</button>
      <form class="form" data-form="today:importManual" data-date="${date}">
        <label class="field">Claude’un cevabı<textarea class="input mono" name="cevap" required placeholder='{"tarih": "...", "plan": [...]}'></textarea></label>
        <button class="btn primary block" type="submit">Planı içe aktar</button>
      </form>
    </div>`,
    'Manuel mod'
  );
}

function showPreview(app, date, parsed, source) {
  pending = { date, parsed, source };
  const keep = (app.s.plans[date]?.items || []).filter((i) => i.durum === 'tamam').length;
  const total = parsed.items.reduce((a, p) => a + p.dk, 0);
  setSheet(
    `${sheetHead('Önerilen plan', L.longDate(date))}
    <div class="list">
      ${parsed.items
        .map(
          (p) => `<div class="row surface"><div class="row-main" style="cursor:default">
            <div class="row-time"><b>${p.saat}</b><span>${p.dk} dk</span></div>
            <div class="row-body">
              <div class="row-title"><span class="dot" style="background:${L.dersRenk(p.ders)}"></span><span class="t">${esc(p.ders)}</span>${p.carriedId ? '<span class="tag blue">Taşındı</span>' : ''}</div>
              <span class="row-sub">${esc(p.konu || '—')}</span>
            </div></div></div>`
        )
        .join('')}
    </div>
    <p class="hint" style="margin:8px 0 12px">Toplam ${L.fmtMin(total)}${keep ? ` · Tamamladığın ${keep} oturum korunacak` : ''}</p>
    ${parsed.not ? `<div class="note surface" style="margin-bottom:14px">${icon('sparkles', 18)}<p>${esc(parsed.not)}</p></div>` : ''}
    <div class="btn-row">
      <button class="btn surface" data-action="today:aiSheet">${icon('refresh', 16)} Yeniden</button>
      <button class="btn primary" data-action="today:applyPending">${icon('check', 16)} Planı uygula</button>
    </div>`,
    'Önerilen plan'
  );
}

// ---------- Eylemler ----------
export const actions = {
  day(el, app) {
    dayOffset = Number(el.dataset.off) || 0;
    app.render();
  },

  toggle(el, app) {
    const { id, date } = el.dataset;
    let msg = '';
    app.update((s) => {
      const it = L.findItem(s, date, id);
      if (!it) return;
      if (it.durum === 'tamam') {
        L.uncompleteItem(s, date, id);
      } else if (s.timer?.itemId === id) {
        const min = finishTimer(s);
        if (min >= 1) msg = `${L.fmtMin(min)} kaydedildi.`;
        if (it.durum !== 'tamam') L.completeItem(s, date, id);
      } else {
        L.completeItem(s, date, id);
      }
    });
    closeSheet();
    if (msg) toast(msg);
  },

  itemSheet(el, app) {
    openItemSheet(app, el.dataset.date, el.dataset.id);
  },

  start(el, app) {
    const { id, date } = el.dataset;
    if (app.s.timer) throw new Error('Önce çalışan kronometreyi bitir.');
    const it = L.findItem(app.s, date, id);
    if (!it) return;
    app.update((s) => {
      s.timer = { itemId: id, date, ders: it.ders, konu: it.konu, taskId: it.taskId || null, startedAt: Date.now(), acc: 0 };
    });
    closeSheet();
  },

  postpone(el, app) {
    const { id, date } = el.dataset;
    app.update((s) => {
      const it = L.findItem(s, date, id);
      if (it) it.durum = 'ertelendi';
      if (s.timer?.itemId === id) s.timer = null;
    });
    closeSheet();
    toast('Ertelendi. Bir sonraki planda Claude bunu yeniden yerleştirir.');
  },

  edit(el, app) {
    openEditSheet(app, el.dataset.date, el.dataset.id);
  },

  remove(el, app) {
    const { id, date } = el.dataset;
    app.update((s) => {
      const p = s.plans[date];
      if (!p) return;
      p.items = p.items.filter((i) => i.id !== id);
      s.sessions = s.sessions.filter((x) => !(x.itemId === id && x.kaynak === 'manuel'));
      if (s.timer?.itemId === id) s.timer = null;
    });
    closeSheet();
    toast('Oturum silindi.');
  },

  addItem(el, app) {
    openEditSheet(app, viewDate(app), null);
  },

  freeTimer(el, app) {
    openSheet(
      `${sheetHead('Kronometre', 'Plan dışı çalışma')}
      <form class="form" data-form="today:freeTimer">
        <label class="field">Ders<select class="input" name="ders">${options(L.DERSLER, 'Matematik')}</select></label>
        <label class="field">Konu<input class="input" name="konu" maxlength="120" placeholder="Örn: Paragraf – 40 soru"></label>
        ${taskOptions(app.s, null)}
        <button class="btn primary block" type="submit">${icon('play', 16)} Başlat</button>
      </form>`,
      'Kronometre'
    );
  },

  timerToggle(el, app) {
    app.update((s) => {
      const t = s.timer;
      if (!t) return;
      if (t.startedAt) {
        t.acc = L.timerElapsedMs(t);
        t.startedAt = null;
      } else {
        t.startedAt = Date.now();
      }
    });
  },

  timerFinish(el, app) {
    let min = 0;
    app.update((s) => {
      min = finishTimer(s);
    });
    toast(min >= 1 ? `${L.fmtMin(min)} kaydedildi.` : 'Bir dakikadan kısa süreler kaydedilmez.');
  },

  timerCancel() {
    openSheet(
      `${sheetHead('Kronometre iptal edilsin mi?')}
      <p class="hint" style="margin-bottom:16px">Geçen süre kaydedilmeden silinir.</p>
      <div class="btn-row">
        <button class="btn surface" data-action="ui:closeSheet">Vazgeç</button>
        <button class="btn danger" data-action="today:timerDiscard">${icon('trash', 16)} İptal et</button>
      </div>`,
      'Kronometre iptali'
    );
  },

  timerDiscard(el, app) {
    app.update((s) => {
      s.timer = null;
    });
    closeSheet();
  },

  aiSheet(el, app) {
    openAiSheet(app);
  },

  async copyPrompt() {
    const ta = document.getElementById('manual-prompt');
    const ok = ta && (await copyText(ta.value));
    toast(ok ? 'Kopyalandı. Şimdi Claude uygulamasına yapıştır.' : 'Kopyalanamadı. Metni uzun basıp kendin seç.', ok ? 'info' : 'error');
  },

  applyPending(el, app) {
    if (!pending) return;
    const { date, parsed, source } = pending;
    pending = null;
    dayOffset = Math.max(0, L.daysBetween(app.today, date));
    app.update((s) => {
      L.applyPlan(s, date, parsed, source);
    });
    closeSheet();
    toast('Plan hazır.');
  }
};

// ---------- Formlar ----------
export const forms = {
  saveItem(data, form, app) {
    const date = form.dataset.date;
    const id = form.dataset.id;
    const dk = Math.round(Number(data.dk));
    if (!/^\d{2}:\d{2}$/.test(data.saat || '')) throw new Error('Bir saat seç.');
    if (!(dk >= 5 && dk <= 240)) throw new Error('Süre 5 ile 240 dakika arasında olmalı.');
    const fields = { saat: data.saat, ders: data.ders, konu: (data.konu || '').trim(), dk, taskId: data.taskId || null };
    app.update((s) => {
      if (!s.plans[date]) s.plans[date] = { items: [], not: '', source: 'manual', createdAt: Date.now() };
      const p = s.plans[date];
      if (id) {
        const it = p.items.find((i) => i.id === id);
        if (it) Object.assign(it, fields);
      } else {
        p.items.push({ id: L.uid(), ...fields, durum: 'bekliyor' });
      }
      p.items = L.sortItems(p.items);
    });
    closeSheet();
    toast(id ? 'Güncellendi.' : 'Oturum eklendi.');
  },

  freeTimer(data, form, app) {
    if (app.s.timer) throw new Error('Zaten çalışan bir kronometre var.');
    app.update((s) => {
      s.timer = {
        itemId: null, date: L.dateStr(), ders: data.ders, konu: (data.konu || '').trim(),
        taskId: data.taskId || null, startedAt: Date.now(), acc: 0
      };
    });
    closeSheet();
  },

  async ai(data, form, app) {
    const date = L.addDays(app.today, Number(data.gun) || 0);
    const s = app.s;
    const user = L.buildUserMessage(L.buildPromptData(s, date, (data.istek || '').trim()));
    const manual = s.settings.aiMode === 'manual' || !s.settings.apiKey;
    if (manual) {
      showManual(date, `${L.SYSTEM_PROMPT}\n\n${user}`);
      return;
    }
    const btn = form.querySelector('button[type="submit"]');
    btn.disabled = true;
    btn.innerHTML = '<span class="spinner"></span> Claude düşünüyor…';
    try {
      const text = await askClaude({ apiKey: s.settings.apiKey, model: s.settings.model, system: L.SYSTEM_PROMPT, user });
      const parsed = L.parsePlan(text);
      if (!parsed.items.length) throw new Error('Claude bu gün için oturum önermedi. İsteğini değiştirip tekrar dene.');
      showPreview(app, date, parsed, 'ai');
    } catch (e) {
      btn.disabled = false;
      btn.innerHTML = `${icon('sparkles', 16)} Tekrar dene`;
      throw e;
    }
  },

  importManual(data, form, app) {
    const parsed = L.parsePlan(data.cevap);
    if (!parsed.items.length) throw new Error('Cevapta hiç oturum yok.');
    showPreview(app, form.dataset.date, parsed, 'manual');
  }
};
