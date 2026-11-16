// Uygulamanın giriş noktası: yönlendirme, olaylar, kronometre ve bildirim döngüsü.
import { store } from './store.js';
import * as L from './logic.js';
import { icon, toast, openSheet, closeSheet, sheetOpen } from './ui.js';
import * as native from './native.js';
import * as today from './views/today.js';
import * as program from './views/program.js';
import * as exams from './views/exams.js';
import * as stats from './views/stats.js';
import * as settings from './views/settings.js';

const views = { today, program, exams, stats, settings };
const TABS = [
  ['today', 'Bugün', 'calendar'],
  ['program', 'Program', 'list'],
  ['exams', 'Denemeler', 'clipboard'],
  ['stats', 'İstatistik', 'chart']
];

const viewEl = document.getElementById('view');
const tabEl = document.getElementById('tabbar');

const app = {
  route: 'today',
  updateInfo: null,
  get s() {
    return store.state;
  },
  get today() {
    return L.dateStr();
  },
  update: (fn) => store.update(fn),
  render: () => render(),
  go(route) {
    if (!views[route]) return;
    closeSheet(true);
    app.route = route;
    render();
    window.scrollTo(0, 0);
    viewEl.focus({ preventScroll: true });
  },
  checkUpdate: (force) => checkUpdate(force),
  toast,
  openSheet,
  closeSheet
};

function applyTheme() {
  const st = store.state.settings;
  document.documentElement.style.setProperty('--accent', st.accent || '#7DD3FC');
  document.body.classList.toggle('reduce', !!st.reduceEffects);
}

function renderTabs() {
  tabEl.innerHTML = TABS.map(
    ([r, label, ic]) =>
      `<button class="tab" data-action="ui:go" data-route="${r}"${app.route === r ? ' aria-current="page"' : ''}>${icon(ic, 20)}<span>${label}</span></button>`
  ).join('');
}

let lastDay = L.dateStr();
function render() {
  lastDay = L.dateStr();
  applyTheme();
  const v = views[app.route];
  viewEl.innerHTML = v.render(app);
  renderTabs();
}

// ---------- Olay yönlendirme ----------
const uiActions = {
  go: (el) => app.go(el.dataset.route),
  closeSheet: () => closeSheet()
};

function resolve(kind, key) {
  const [ns, name] = String(key).split(':');
  if (ns === 'ui') return kind === 'actions' ? uiActions[name] : null;
  return views[ns]?.[kind]?.[name] || null;
}

function report(err) {
  console.error(err);
  toast(err?.message || 'Bir hata oluştu.', 'error');
}

document.addEventListener('click', (e) => {
  const el = e.target.closest('[data-action]');
  if (!el) return;
  const fn = resolve('actions', el.dataset.action);
  if (!fn) return;
  e.preventDefault();
  Promise.resolve()
    .then(() => fn(el, app, e))
    .catch(report);
});

document.addEventListener('submit', (e) => {
  const form = e.target.closest('form[data-form]');
  if (!form) return;
  e.preventDefault();
  const fn = resolve('forms', form.dataset.form);
  if (!fn) return;
  const data = Object.fromEntries(new FormData(form));
  Promise.resolve()
    .then(() => fn(data, form, app))
    .catch(report);
});

document.addEventListener('input', (e) => {
  const form = e.target.closest('form[data-live]');
  if (!form) return;
  try {
    resolve('live', form.dataset.live)?.(form, app, e);
  } catch (err) {
    report(err);
  }
});

document.addEventListener('change', (e) => {
  const el = e.target.closest('[data-setting]');
  if (!el) return;
  try {
    settings.applySetting(el, app);
  } catch (err) {
    report(err);
  }
});

// ---------- Kronometre ve gün değişimi ----------
setInterval(() => {
  if (L.dateStr() !== lastDay && !sheetOpen()) render();
  const t = store.state.timer;
  if (!t || !t.startedAt) return;
  const txt = L.fmtClock(L.timerElapsedMs(t) / 1000);
  document.querySelectorAll('[data-timer]').forEach((el) => {
    el.textContent = txt;
  });
}, 1000);

// ---------- Bildirimler ----------
let notifSig = null;
let notifTimer = null;
function refreshNotifications() {
  clearTimeout(notifTimer);
  notifTimer = setTimeout(async () => {
    const st = store.state;
    const list = st.settings.notifications ? L.notificationsFor(st, new Date(), Number(st.settings.notifyBefore) || 0) : [];
    const sig = list.map((n) => `${n.id}@${n.at.getTime()}@${n.title}`).join('|');
    if (sig === notifSig) return;
    notifSig = sig;
    const ids = await native.scheduleNotifications(list, st.notifIds || []);
    store.state.notifIds = ids;
    store.save();
  }, 1200);
}

// ---------- Güncelleme ----------
async function checkUpdate(force = false) {
  if (!native.isNative() && !force) return null;
  try {
    const info = await native.checkUpdate();
    app.updateInfo = info;
    if (info.newer && app.route === 'today') render();
    return info;
  } catch (e) {
    if (force) throw e;
    return null;
  }
}

// ---------- Başlangıç ----------
store.load();
const startView = new URLSearchParams(location.search).get('view');
if (views[startView]) app.route = startView;
store.subscribe(() => {
  render();
  refreshNotifications();
});
render();
refreshNotifications();
native.initSystemBars();
native.onBackButton(() => {
  if (sheetOpen()) {
    closeSheet();
    return true;
  }
  if (app.route !== 'today') {
    app.go('today');
    return true;
  }
  return false;
});
checkUpdate(false);
