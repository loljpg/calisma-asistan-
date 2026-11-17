// Telefon ile Firebase arasındaki eşitleme.
//
// users/{uid}/
//   backup            Uygulamanın tüm verisi (API anahtarı hariç), en fazla 20 saniyede bir
//   device/view       Masa saatinin göstereceği bugünün planı ve özeti
//   device/events     Masa saatinin gönderdiği olaylar; uygulama işleyip siler
//   device/status     Masa saatinin son görülme zamanı ve sürümü
import { store } from './store.js';
import * as L from './logic.js';
import * as cloud from './cloud.js';

export const syncState = { status: null, lastSync: 0, error: '', applied: 0 };

let busy = false;
let lastViewSig = '';
let lastBackupSig = '';
let lastBackupAt = 0;

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
