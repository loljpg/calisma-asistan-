// Telefon ile Firebase arasındaki eşitleme.
//
// users/{uid}/
//   backup            Uygulamanın tüm verisi (API anahtarı hariç), en fazla 20 saniyede bir
//   device/view       Masa saatinin göstereceği bugünün planı ve özeti
//   device/events     Masa saatinin gönderdiği olaylar; uygulama işleyip siler
//   device/status     Masa saatinin son görülme zamanı ve sürümü
//   device/timer      Ortak kronometre; telefon ve masa saati aynı kaydı okuyup yazar
import { store } from './store.js';
import * as L from './logic.js';
import * as cloud from './cloud.js';

export const syncState = { status: null, lastSync: 0, error: '', applied: 0 };

let busy = false;
let lastViewSig = '';
let lastBackupSig = '';
let lastBackupAt = 0;
let lastTimerSig = null;   // en son buluta yazılan ya da buluttan alınan kronometre
let timerRev = 0;          // görülen en yeni kronometre sürümü

export const isSignedIn = () => !!store.state.settings.cloud?.refreshToken;

async function getAuth() {
  const c = store.state.settings.cloud;
  if (!c?.refreshToken) throw new Error('Buluta giriş yapılmamış.');
  if (!c.idToken || Date.now() > (c.expiresAt || 0) - 60000) {
    try {
      Object.assign(c, await cloud.refreshToken(c));
      store.save();
    } catch (e) {
      if (e.authExpired) {
        c.idToken = '';
        c.refreshToken = '';
        store.save();
      }
      throw e;
    }
  }
  return { dbUrl: c.dbUrl, idToken: c.idToken };
}

const db = cloud.makeDb(getAuth);
const root = () => `users/${store.state.settings.cloud.uid}`;

export function resetSyncMemory() {
  lastViewSig = '';
  lastBackupSig = '';
  lastBackupAt = 0;
  lastTimerSig = null;
  timerRev = 0;
  syncState.status = null;
  syncState.lastSync = 0;
  syncState.error = '';
}

// force: görünümü ve yedeği değişmemiş olsa da yazar.
// flush: yedek değiştiyse 20 saniye beklemeden yazar (uygulama arka plana geçerken).
export async function syncNow({ force = false, flush = false } = {}) {
  if (!isSignedIn() || busy) return null;
  busy = true;
  try {
    const base = root();

    // 0. Ortak kronometre (canlı bağlantı yoksa buradan okunur)
    if (!live.open) await pullTimer();
    else if (lastTimerSig !== L.timerSig(store.state.timer)) await pushTimer();

    // 1. Masa saatinden gelen olaylar
    const events = await db.get(`${base}/device/events`);
    syncState.applied = 0;
    if (events && typeof events === 'object') {
      const keys = Object.keys(events);
      if (keys.length) {
        let applied = 0;
        store.update((s) => {
          applied = L.applyDeviceEvents(s, events);
        });
        syncState.applied = applied;
        await db.patch(`${base}/device/events`, Object.fromEntries(keys.map((k) => [k, null])));
      }
    }

    // 2. Masa saatinin göstereceği özet
    const view = L.deviceView(store.state);
    const viewSig = JSON.stringify(view);
    if (force || viewSig !== lastViewSig) {
      await db.put(`${base}/device/view`, { ...view, updatedAt: { '.sv': 'timestamp' } });
      lastViewSig = viewSig;
    }

    // 3. Bulut yedeği
    const backup = store.backupData();
    const backupSig = JSON.stringify(backup);
    if (force || (backupSig !== lastBackupSig && (flush || Date.now() - lastBackupAt > 20000))) {
      await db.put(`${base}/backup`, { savedAt: { '.sv': 'timestamp' }, data: backup });
      lastBackupSig = backupSig;
      lastBackupAt = Date.now();
    }

    // 4. Masa saatinin durumu
    syncState.status = await db.get(`${base}/device/status`);
    syncState.lastSync = Date.now();
    syncState.error = '';
    return syncState;
  } catch (e) {
    syncState.error = e.message || 'Eşitleme hatası';
    throw e;
  } finally {
    busy = false;
  }
}

// ---------- Ortak kronometre ----------
// Kronometre sürümü buluta ve Bluetooth'a giden kayıtlarda ortaktır.
export function nextTimerRev() {
  timerRev = Math.max(Date.now(), timerRev + 1);
  return timerRev;
}

// Masa saatinden Bluetooth ile gelen kronometre: uygulanır, buluttaki kayıt da güncellenir.
export function relayDeviceTimer(r) {
  if (!r || typeof r !== 'object') return false;
  const changed = takeRemoteTimer(r);
  if (isSignedIn() && Number(r.rev) === timerRev) db.put(`${root()}/device/timer`, r).catch(() => {});
  return changed;
}

// Telefonda kronometre değiştiyse hemen buluta yazar; masa saati canlı bağlantıyla anında görür.
let pushing = null;
export function pushTimer() {
  if (!isSignedIn()) return Promise.resolve();
  if (pushing) return pushing.then(() => pushTimer());
  const sig = L.timerSig(store.state.timer);
  if (sig === lastTimerSig) return Promise.resolve();
  const body = L.sharedTimer(store.state, 'phone', nextTimerRev());
  pushing = db.put(`${root()}/device/timer`, body)
    .then(() => { lastTimerSig = sig; })
    .catch((e) => { console.warn('Kronometre', e); })
    .finally(() => { pushing = null; });
  return pushing;
}

// Buluttaki kronometre telefondakinden yeniyse uygular. İlk okumada yalnızca masa saati
// yazdıysa uygulanır; telefonun kendi eski kaydı yeni açılan uygulamayı ezmesin.
function takeRemoteTimer(r) {
  if (!r || typeof r !== 'object') {
    if (lastTimerSig === null) lastTimerSig = 'off-remote';
    return false;
  }
  const rev = Number(r.rev) || 0;
  const first = lastTimerSig === null;
  if (!first && rev <= timerRev) return false;
  if (first && r.by === 'phone') {
    timerRev = Math.max(timerRev, rev);
    lastTimerSig = 'phone-old';
    return false;
  }
  timerRev = Math.max(timerRev, rev);
  let changed = false;
  store.update((s) => {
    changed = L.applySharedTimer(s, r);
    lastTimerSig = L.timerSig(s.timer);
  });
  return changed;
}

async function pullTimer() {
  const r = await db.get(`${root()}/device/timer`);
  takeRemoteTimer(r);
  if (lastTimerSig !== L.timerSig(store.state.timer)) await pushTimer();
}

// ---------- Canlı bağlantı ----------
// Realtime Database'in REST akışı (Server-Sent Events): masa saatindeki değişiklikler
// saniyesinde gelir. Bağlantı koparsa 30 saniyelik eşitleme yedekte çalışmaya devam eder.
const live = { es: null, open: false, wanted: false, retry: null, delay: 5000, onChange: null, onEvents: null };

export function startLive({ onChange, onEvents } = {}) {
  if (onChange) live.onChange = onChange;
  if (onEvents) live.onEvents = onEvents;
  live.wanted = true;
  if (live.es || live.retry || typeof EventSource === 'undefined' || !isSignedIn()) return;
  openLive();
}

export function stopLive() {
  live.wanted = false;
  clearTimeout(live.retry);
  live.retry = null;
  closeLive();
}

export const liveOpen = () => live.open;

function closeLive() {
  if (live.es) live.es.close();
  live.es = null;
  live.open = false;
}

function retryLive(ms = live.delay) {
  closeLive();
  if (!live.wanted) return;
  clearTimeout(live.retry);
  live.retry = setTimeout(() => {
    live.retry = null;
    if (live.wanted && isSignedIn()) openLive();
  }, ms);
  live.delay = Math.min(live.delay * 2, 60000);
}

async function openLive() {
  let auth;
  try {
    auth = await getAuth();
  } catch {
    retryLive();
    return;
  }
  if (!live.wanted || live.es) return;
  const url = `${auth.dbUrl}/${root()}/device.json?auth=${encodeURIComponent(auth.idToken)}`;
  const es = new EventSource(url);
  live.es = es;
  const onData = (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (!live.open) {
      live.open = true;
      live.delay = 5000;
    }
    handleLive(ev.type, msg?.path || '/', msg?.data);
  };
  es.addEventListener('put', onData);
  es.addEventListener('patch', onData);
  es.addEventListener('keep-alive', () => {
    live.open = true;
  });
  // Oturum anahtarı süresi dolunca Firebase akışı kapatır; yeni anahtarla yeniden açılır.
  es.addEventListener('auth_revoked', () => retryLive(500));
  es.addEventListener('cancel', () => retryLive(30000));
  es.onerror = () => {
    const wasOpen = live.open;
    live.open = false;
    if (es.readyState === EventSource.CLOSED || !wasOpen) retryLive();
  };
}

function handleLive(type, path, data) {
  const parts = path.split('/').filter(Boolean);
  const key = parts[0];
  if (!key && type === 'patch') {
    for (const [k, v] of Object.entries(data || {})) handleLive('put', `/${k}`, v);
    return;
  }
  if (!key) {
    // İlk mesaj: device/ altındaki her şey
    const all = data && typeof data === 'object' ? data : {};
    takeRemoteTimer(all.timer);
    if (lastTimerSig !== L.timerSig(store.state.timer)) pushTimer();
    setStatus(all.status);
    if (all.events && Object.keys(all.events).length) live.onEvents?.();
    return;
  }
  if (key === 'timer') {
    if (parts.length === 1 && type === 'put') takeRemoteTimer(data);
    else db.get(`${root()}/device/timer`).then(takeRemoteTimer).catch(() => {});
  } else if (key === 'events') {
    // Telefonun işlenen olayları silmesi de buraya düşer; onlar yok sayılır.
    const added = type === 'patch'
      ? Object.values(data || {}).some((v) => v !== null)
      : data !== null;
    if (added) live.onEvents?.();
  } else if (key === 'status') {
    if (parts.length === 1 && type === 'put') setStatus(data);
    else db.get(`${root()}/device/status`).then(setStatus).catch(() => {});
  }
}

function setStatus(st) {
  syncState.status = st && typeof st === 'object' ? st : null;
  live.onChange?.();
}

export async function connect({ dbUrl, apiKey, email, password, create }) {
  const url = cloud.normalizeDbUrl(dbUrl);
  const key = String(apiKey || '').trim();
  if (!key) throw new Error('Web API anahtarını gir.');
  const session = await cloud.signIn({ apiKey: key, email: String(email || '').trim(), password, create });
  store.update((s) => {
    s.settings.cloud = { dbUrl: url, apiKey: key, ...session };
  });
  resetSyncMemory();
  try {
    await syncNow({ force: true });
  } catch (e) {
    // İlk eşitleme olmadıysa (yanlış adres, kurallar) girişi geri al ki bilgiler düzeltilebilsin.
    store.update((s) => {
      s.settings.cloud = { dbUrl: url, apiKey: key, email: session.email };
    });
    throw e;
  }
}

export function disconnect() {
  stopLive();
  store.update((s) => {
    const c = s.settings.cloud || {};
    s.settings.cloud = { dbUrl: c.dbUrl, apiKey: c.apiKey, email: c.email };
  });
  resetSyncMemory();
}

export async function restoreFromCloud() {
  const b = await db.get(`${root()}/backup`);
  if (!b || !b.data) throw new Error('Bulutta henüz yedek yok.');
  store.importJSON(JSON.stringify({ data: b.data }));
  lastBackupSig = JSON.stringify(store.backupData());
}
