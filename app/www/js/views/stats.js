// İstatistik ekranı: son 7 günün çalışma süreleri ve plan uyumu.
import * as L from '../logic.js';
import { icon, esc, pageHead } from '../ui.js';

export function render(app) {
  const s = app.s;
  const today = app.today;
  const days = L.lastDays(s.sessions, today, 7);
  const total = days.reduce((a, d) => a + d.min, 0);
  const max = Math.max(60, ...days.map((d) => d.min));
  const subjects = L.bySubject(s.sessions, L.addDays(today, -6), today);
  const sMax = Math.max(1, ...subjects.map((x) => x.min));

  let planned = 0;
  let done = 0;
  for (let i = 0; i < 7; i++) {
    for (const it of s.plans[L.addDays(today, -i)]?.items || []) {
      if (it.durum === 'tasindi') continue;
      planned++;
      if (it.durum === 'tamam') done++;
    }
  }
  const tasksDone = s.tasks.filter((t) => t.done).length;

  return `
  ${pageHead('İstatistik', 'Son 7 gün')}
  <section class="card surface" aria-label="Haftalık çalışma">
    <div class="spread">
      <span class="eyebrow">Bu hafta</span>
      <span class="chip">${icon('flame', 14)} ${L.streak(s.sessions, today)} gün seri</span>
    </div>
    <div class="big-row"><span class="big">${L.fmtMin(total)}</span><span class="muted">günde ort. ${L.fmtMin(total / 7)}</span></div>
    <div class="bars" style="--n:7">
      ${days.map((d) => `<div class="bar-col${d.date === today ? ' hot' : ''}"><em>${d.min || ''}</em><i style="height:${Math.round((d.min / max) * 90)}px"></i><span>${d.label}</span></div>`).join('')}
    </div>
  </section>
  <section class="card surface" aria-label="Derslere göre">
    <span class="eyebrow">Derslere göre</span>
    ${subjects.length
      ? subjects.map((x) => `<div class="hbar"><span>${esc(x.ders)}</span><div class="progress"><i style="width:${Math.round((x.min / sMax) * 100)}%;background:${L.dersRenk(x.ders)}"></i></div><span>${L.fmtMin(x.min)}</span></div>`).join('')
      : '<p class="muted small">Bu hafta henüz kayıtlı çalışma yok. Bir oturumu tamamladığında ya da kronometreyi bitirdiğinde burada görünür.</p>'}
  </section>
  <div class="grid2">
    <div class="card tight surface">
      <span class="eyebrow">Plan uyumu</span>
      <span class="big" style="font-size:30px">%${planned ? Math.round((done / planned) * 100) : 0}</span>
      <span class="muted small">${done}/${planned} oturum</span>
    </div>
    <div class="card tight surface">
      <span class="eyebrow">Ödevler</span>
      <span class="big" style="font-size:30px">${tasksDone}</span>
      <span class="muted small">tamamlandı · ${s.tasks.length - tasksDone} aktif</span>
    </div>
  </div>`;
}
