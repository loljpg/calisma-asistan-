// Denemeler ekranı: TYT/AYT denemeleri, quizler ve kazanım testleri.
import * as L from '../logic.js';
import { icon, esc, options, radio, pageHead, openSheet, closeSheet, sheetHead, toast } from '../ui.js';

let tab = 'exams';
let type = 'TYT';

export function render(app) {
  return `
  ${pageHead('Denemeler', 'Sonuçlar ve kazanımlar', `<button class="icon-btn glass" data-action="exams:add" aria-label="Sonuç ekle">${icon('plus')}</button>`)}
  <div class="seg glass" role="group" aria-label="Sonuç türü">
    <button aria-pressed="${tab === 'exams'}" data-action="exams:tab" data-tab="exams">Denemeler</button>
    <button aria-pressed="${tab === 'tests'}" data-action="exams:tab" data-tab="tests">Quiz ve testler</button>
  </div>
  ${tab === 'exams' ? examsView(app) : testsView(app)}`;
}

// ---------- Denemeler ----------
function examsView(app) {
  const list = app.s.exams.filter((e) => e.tur === type).sort((a, b) => (a.tarih < b.tarih ? -1 : 1));
  const typeSeg = `<div class="seg small glass" role="group" aria-label="Sınav">
    <button aria-pressed="${type === 'TYT'}" data-action="exams:type" data-type="TYT">TYT</button>
    <button aria-pressed="${type === 'AYT'}" data-action="exams:type" data-type="AYT">AYT</button>
  </div>`;
  if (!list.length) {
    return `${typeSeg}
    <div class="empty glass">
      <b>Henüz ${type} denemesi yok</b>
      <p>Deneme sonuçlarını girdiğinde net gelişimini burada görürsün. Claude da zayıf derslerini plana öncelikli koyar.</p>
      <button class="btn primary" data-action="exams:add">${icon('plus', 16)} Deneme ekle</button>
    </div>`;
  }
  const last = list[list.length - 1];
  const prev = list.length > 1 ? list[list.length - 2] : null;
  const lastNet = L.examNet(last);
  const delta = prev ? lastNet - L.examNet(prev) : null;
  const recent = list.slice(-8);
  const nets = recent.map(L.examNet);
  const max = Math.max(10, ...nets) * 1.1;
  return `
  ${typeSeg}
  <section class="card glass" aria-label="Net gelişimi">
    <div class="spread">
      <div style="display:flex;flex-direction:column;gap:4px">
        <span class="eyebrow">${type} net · ${esc(last.ad)}</span>
        <span class="big">${L.fmtNum(lastNet)}</span>
      </div>
      ${delta === null ? '' : `<span class="chip ${delta >= 0 ? 'ok' : 'warn'}">${delta >= 0 ? '+' : ''}${L.fmtNum(delta)}</span>`}
    </div>
    <div class="bars" style="--n:${recent.length}">
      ${recent
        .map((e, i) => `<div class="bar-col${i === recent.length - 1 ? ' hot' : ''}"><em>${L.fmtNum(nets[i])}</em><i style="height:${Math.round((Math.max(0, nets[i]) / max) * 90)}px"></i><span>${L.shortDate(e.tarih)}</span></div>`)
        .join('')}
    </div>
  </section>
  <div class="grid2">
    ${last.dersler
      .map((x) => {
        const n = L.net(x.d, x.y);
        const pct = x.max ? Math.max(0, Math.min(100, Math.round((n / x.max) * 100))) : 0;
        return `<div class="card tight glass">
          <span class="muted small">${esc(x.ders)}</span>
          <b style="font-size:18px">${L.fmtNum(n)}${x.max ? ` <span class="muted small">/ ${x.max}</span>` : ''}</b>
          ${x.max ? `<div class="progress thin"><i style="width:${pct}%"></i></div>` : ''}
        </div>`;
      })
      .join('')}
  </div>
  <h2 class="set-title">Tüm ${type} denemeleri</h2>
  <div class="list">
    ${[...list].reverse().map((e) => `
      <div class="row glass"><button class="row-main" data-action="exams:examSheet" data-id="${e.id}">
        <div class="row-time"><b>${L.fmtNum(L.examNet(e))}</b><span>net</span></div>
        <div class="row-body"><div class="row-title"><span class="t">${esc(e.ad)}</span></div><span class="row-sub">${L.longDate(e.tarih)}</span></div>
      </button></div>`).join('')}
  </div>`;
}

function examRows(tur) {
  return `<div class="exam-row head"><span>Ders</span><span>Doğru</span><span>Yanlış</span></div>${L.SINAV_BOLUMLERI[tur]
    .map(
      (b, i) => `<div class="exam-row">
        <span>${esc(b.ders)}${b.max ? ` <span class="muted small">(${b.max})</span>` : ''}</span>
        <input class="input" type="number" inputmode="numeric" min="0" ${b.max ? `max="${b.max}"` : ''} name="d_${i}" aria-label="${esc(b.ders)} doğru">
        <input class="input" type="number" inputmode="numeric" min="0" ${b.max ? `max="${b.max}"` : ''} name="y_${i}" aria-label="${esc(b.ders)} yanlış">
      </div>`
    )
    .join('')}`;
}

function openExamForm(app) {
  const n = app.s.exams.filter((e) => e.tur === type).length + 1;
  openSheet(
    `${sheetHead('Deneme ekle')}
    <form class="form" data-form="exams:saveExam" data-live="exams:examForm">
      <div class="radio-row">${radio('tur', 'TYT', 'TYT', type === 'TYT')}${radio('tur', 'AYT', 'AYT', type === 'AYT')}</div>
      <div class="grid2">
        <label class="field">Adı<input class="input" name="ad" maxlength="60" value="${type} Deneme ${n}"></label>
        <label class="field">Tarih<input class="input" type="date" name="tarih" value="${app.today}" required></label>
      </div>
      <div class="exam-rows" data-rows>${examRows(type)}</div>
      <div class="spread"><span class="muted">Toplam net</span><b style="font-size:28px" data-net>0</b></div>
      <p class="hint">Net = doğru − yanlış ÷ 4. Boş bıraktığın dersler kaydedilmez.</p>
      <button class="btn primary block" type="submit">Kaydet</button>
    </form>`,
    'Deneme ekle'
  );
}

// ---------- Quiz ve testler ----------
function testsView(app) {
  const tests = [...app.s.tests].sort((a, b) => (a.tarih < b.tarih ? 1 : -1));
  if (!tests.length) {
    return `
    <div class="empty glass">
      <b>Henüz quiz ya da test yok</b>
      <p>Quiz ve kazanım kavrama testi sonuçlarını ekle. %60'ın altında kalan konular zayıf olarak işaretlenir ve plana öncelikli girer.</p>
      <button class="btn primary" data-action="exams:add">${icon('plus', 16)} Sonuç ekle</button>
    </div>`;
  }
  const weak = L.weakTopics(app.s.tests);
  return `
  ${weak.length ? `<section class="card tight glass" aria-label="Zayıf kazanımlar">
    <div style="display:flex;align-items:center;gap:8px;color:var(--warn)">${icon('alert', 16)}<h2 style="font-size:15px;color:#FFFFFF">Zayıf kazanımlar</h2></div>
    <div class="chips">${weak.map((t) => `<span class="chip warn">${esc(t.ders)} · ${esc(t.konu)} · %${L.testPct(t)}</span>`).join('')}</div>
  </section>` : ''}
  <div class="list">
    ${tests.map((t) => {
      const pct = L.testPct(t);
      return `<div class="row glass"><button class="row-main" data-action="exams:testSheet" data-id="${t.id}">
        <div class="row-time"><b style="color:${pct < 60 ? 'var(--warn)' : 'inherit'}">%${pct}</b><span>${t.dogru}/${t.toplam}</span></div>
        <div class="row-body">
          <div class="row-title"><span class="dot" style="background:${L.dersRenk(t.ders)}"></span><span class="t">${esc(t.ders)} · ${esc(t.konu)}</span></div>
          <span class="row-sub">${t.tur === 'kazanim' ? 'Kazanım testi' : 'Quiz'} · ${L.shortDate(t.tarih)}</span>
        </div>
      </button></div>`;
    }).join('')}
  </div>`;
}

function openTestForm(app) {
  openSheet(
    `${sheetHead('Quiz / test ekle')}
    <form class="form" data-form="exams:saveTest">
      <div class="radio-row">${radio('tur', 'quiz', 'Quiz', true)}${radio('tur', 'kazanim', 'Kazanım testi', false)}</div>
      <label class="field">Ders<select class="input" name="ders">${options(L.DERSLER, 'Matematik')}</select></label>
      <label class="field">Konu / kazanım<input class="input" name="konu" required maxlength="100" placeholder="Örn: Bileşke fonksiyon"></label>
      <div class="grid2">
        <label class="field">Doğru<input class="input" type="number" inputmode="numeric" name="dogru" min="0" required></label>
        <label class="field">Soru sayısı<input class="input" type="number" inputmode="numeric" name="toplam" min="1" required></label>
      </div>
      <label class="field">Tarih<input class="input" type="date" name="tarih" value="${app.today}" required></label>
      <button class="btn primary block" type="submit">Kaydet</button>
    </form>`,
    'Quiz ekle'
  );
}

// ---------- Eylemler ----------
export const actions = {
  tab(el, app) {
    tab = el.dataset.tab;
    app.render();
  },
  type(el, app) {
    type = el.dataset.type;
    app.render();
  },
  add(el, app) {
    if (tab === 'exams') openExamForm(app);
    else openTestForm(app);
  },
  examSheet(el, app) {
    const e = app.s.exams.find((x) => x.id === el.dataset.id);
    if (!e) return;
    openSheet(
      `${sheetHead(e.ad, `${e.tur} · ${L.longDate(e.tarih)}`)}
      <div class="list" style="margin-bottom:14px">
        ${e.dersler.map((x) => `<div class="spread card tight glass"><span>${esc(x.ders)}</span><span class="muted small">${x.d} D · ${x.y} Y</span><b>${L.fmtNum(L.net(x.d, x.y))}</b></div>`).join('')}
      </div>
      <div class="spread" style="margin-bottom:16px"><span class="muted">Toplam net</span><b style="font-size:24px">${L.fmtNum(L.examNet(e))}</b></div>
      <button class="btn danger block" data-action="exams:removeExam" data-id="${e.id}">${icon('trash', 16)} Denemeyi sil</button>`,
      e.ad
    );
  },
  removeExam(el, app) {
    app.update((s) => {
      s.exams = s.exams.filter((x) => x.id !== el.dataset.id);
    });
    closeSheet();
    toast('Deneme silindi.');
  },
  testSheet(el, app) {
    const t = app.s.tests.find((x) => x.id === el.dataset.id);
    if (!t) return;
    openSheet(
      `${sheetHead(`${t.ders} · ${t.konu}`, `${t.tur === 'kazanim' ? 'Kazanım testi' : 'Quiz'} · ${L.longDate(t.tarih)}`)}
      <div class="spread" style="margin-bottom:16px"><span class="muted">${t.dogru} / ${t.toplam} doğru</span><b style="font-size:24px">%${L.testPct(t)}</b></div>
      <button class="btn danger block" data-action="exams:removeTest" data-id="${t.id}">${icon('trash', 16)} Sonucu sil</button>`,
      'Test sonucu'
    );
  },
  removeTest(el, app) {
    app.update((s) => {
      s.tests = s.tests.filter((x) => x.id !== el.dataset.id);
    });
    closeSheet();
    toast('Sonuç silindi.');
  }
};

// Deneme formunda tür değişince satırları yeniler, toplam neti canlı hesaplar.
export const live = {
  examForm(form, app, e) {
    const tur = form.querySelector('input[name="tur"]:checked')?.value || 'TYT';
    if (e?.target?.name === 'tur') {
      form.querySelector('[data-rows]').innerHTML = examRows(tur);
      const ad = form.querySelector('input[name="ad"]');
      if (/^(TYT|AYT) Deneme \d+$/.test(ad.value)) {
        ad.value = `${tur} Deneme ${app.s.exams.filter((x) => x.tur === tur).length + 1}`;
      }
    }
    let total = 0;
    L.SINAV_BOLUMLERI[tur].forEach((b, i) => {
      total += L.net(form.elements[`d_${i}`]?.value, form.elements[`y_${i}`]?.value);
    });
    form.querySelector('[data-net]').textContent = L.fmtNum(total);
  }
};

export const forms = {
  saveExam(data, form, app) {
    const tur = data.tur === 'AYT' ? 'AYT' : 'TYT';
    const dersler = [];
    L.SINAV_BOLUMLERI[tur].forEach((b, i) => {
      const dRaw = data[`d_${i}`];
      const yRaw = data[`y_${i}`];
      if (dRaw === '' && yRaw === '') return;
      const d = Math.max(0, Math.round(Number(dRaw) || 0));
      const y = Math.max(0, Math.round(Number(yRaw) || 0));
      if (b.max && d + y > b.max) throw new Error(`${b.ders}: doğru + yanlış en fazla ${b.max} olabilir.`);
      dersler.push({ ders: b.ders, d, y, max: b.max });
    });
    if (!dersler.length) throw new Error('En az bir dersin sonucunu gir.');
    if (!data.tarih) throw new Error('Tarih seç.');
    app.update((s) => {
      s.exams.push({ id: L.uid(), tur, ad: (data.ad || `${tur} Denemesi`).trim().slice(0, 60), tarih: data.tarih, dersler });
    });
    type = tur;
    tab = 'exams';
    closeSheet();
    app.render();
    toast('Deneme kaydedildi.');
  },
  saveTest(data, form, app) {
    const dogru = Math.round(Number(data.dogru));
    const toplam = Math.round(Number(data.toplam));
    const konu = (data.konu || '').trim();
    if (!konu) throw new Error('Konuyu yaz.');
    if (!(toplam >= 1)) throw new Error('Soru sayısı en az 1 olmalı.');
    if (!(dogru >= 0 && dogru <= toplam)) throw new Error('Doğru sayısı soru sayısından fazla olamaz.');
    app.update((s) => {
      s.tests.push({ id: L.uid(), tur: data.tur === 'kazanim' ? 'kazanim' : 'quiz', ders: data.ders, konu: konu.slice(0, 100), dogru, toplam, tarih: data.tarih });
    });
    tab = 'tests';
    closeSheet();
    app.render();
    toast('Sonuç kaydedildi.');
  }
};
