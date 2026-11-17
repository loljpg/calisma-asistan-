import test from 'node:test';
import assert from 'node:assert/strict';
import * as L from '../www/js/logic.js';
import { store, defaults } from '../www/js/store.js';
import * as cloud from '../www/js/cloud.js';
import { syncNow, connect, restoreFromCloud, syncState, resetSyncMemory } from '../www/js/sync.js';

// Bellekte çalışan sahte Realtime Database + Auth.
function fakeFirebase() {
  const db = {};
  const calls = [];
  const resolveSv = (v) => {
    if (v && typeof v === 'object') {
      if (v['.sv'] === 'timestamp') return 1700000000000;
      const out = Array.isArray(v) ? [] : {};
      for (const [k, x] of Object.entries(v)) out[k] = resolveSv(x);
      return out;
    }
    return v;
  };
  const getAt = (parts) => parts.reduce((o, k) => (o == null ? undefined : o[k]), db);
  const setAt = (parts, value) => {
    let o = db;
    parts.slice(0, -1).forEach((k) => {
      o[k] = o[k] && typeof o[k] === 'object' ? o[k] : {};
      o = o[k];
    });
    if (value === null) delete o[parts.at(-1)];
    else o[parts.at(-1)] = value;
  };
  const json = (status, body) => ({ ok: status < 300, status, text: async () => JSON.stringify(body) });
  globalThis.fetch = async (url, opts = {}) => {
    const u = new URL(url);
    calls.push(`${opts.method || 'GET'} ${u.pathname}`);
    if (u.hostname === 'identitytoolkit.googleapis.com') {
      const body = JSON.parse(opts.body);
      if (body.password === 'yanlis') return json(400, { error: { message: 'INVALID_LOGIN_CREDENTIALS' } });
      return json(200, { localId: 'u1', email: body.email, idToken: 'tok', refreshToken: 'ref', expiresIn: '3600' });
    }
    if (u.hostname === 'securetoken.googleapis.com') return json(200, { id_token: 'tok2', refresh_token: 'ref2', expires_in: '3600' });
    if (u.searchParams.get('auth') == null) return json(401, { error: 'Permission denied' });
    const parts = u.pathname.replace(/^\//, '').replace(/\.json$/, '').split('/');
    const method = opts.method || 'GET';
    if (method === 'GET') return json(200, getAt(parts) ?? null);
    const body = resolveSv(JSON.parse(opts.body));
    if (method === 'PUT') setAt(parts, body);
    if (method === 'PATCH') for (const [k, v] of Object.entries(body)) setAt([...parts, k], v);
    return json(200, body);
  };
  return { db, calls };
}

function freshStore() {
  const mem = new Map();
  store.load({ getItem: (k) => mem.get(k) ?? null, setItem: (k, v) => mem.set(k, v) });
  resetSyncMemory();
}

test('veritabanı adresi düzeltilir ve doğrulanır', () => {
  assert.equal(cloud.normalizeDbUrl('calisma-asistani-default-rtdb.europe-west1.firebasedatabase.app/'), 'https://calisma-asistani-default-rtdb.europe-west1.firebasedatabase.app');
  assert.equal(cloud.normalizeDbUrl('http://x-default-rtdb.firebaseio.com'), 'https://x-default-rtdb.firebaseio.com');
  assert.throws(() => cloud.normalizeDbUrl('https://google.com'), /firebasedatabase/);
});

test('cihaz görünümü bugünün planını ve özeti içerir', () => {
  const s = defaults();
  const today = L.dateStr();
  s.plans[today] = { items: [
    { id: 'a', saat: '10:00', ders: 'Fizik', konu: 'K', dk: 30, durum: 'tamam' },
    { id: 'b', saat: '11:00', ders: 'Kimya', dk: 30, durum: 'tasindi' }
  ] };
  s.sessions.push({ tarih: today, sec: 1800 });
  const v = L.deviceView(s);
  assert.equal(v.date, today);
  assert.deepEqual(v.items.map((i) => i.id), ['a']);
  assert.equal(v.items[0].konu, 'K');
  assert.equal(v.studiedMin, 30);
  assert.equal(v.streak, 1);
});

test('cihaz olayları bir kez uygulanır', () => {
  const s = defaults();
  const d = '2026-11-17';
  s.plans[d] = { items: [
    { id: 'a', saat: '10:00', ders: 'Fizik', konu: 'K', dk: 30, durum: 'bekliyor', taskId: 't1' },
    { id: 'b', saat: '11:00', ders: 'Kimya', konu: 'M', dk: 40, durum: 'bekliyor' }
  ] };
  const events = {
    e2: { type: 'session', itemId: 'b', date: d, sec: 1500, at: 2 },
    e1: { type: 'complete', itemId: 'a', date: d, at: 1 },
    e3: { type: 'session', date: d, sec: 30, at: 3 },
    e4: { type: 'session', date: d, sec: 900, ders: 'Tarih', at: 4 }
  };
  assert.equal(L.applyDeviceEvents(s, events), 3);
  assert.equal(L.findItem(s, d, 'a').durum, 'tamam');
  assert.equal(L.findItem(s, d, 'b').durum, 'tamam');
  assert.equal(s.sessions.find((x) => x.itemId === 'b').sec, 1500);
  assert.equal(s.sessions.find((x) => x.itemId === 'b').kaynak, 'cihaz');
  assert.equal(s.sessions.find((x) => x.ders === 'Tarih').itemId, null);
  assert.equal(L.taskSpent(s.sessions, 't1'), 30);
  assert.equal(L.applyDeviceEvents(s, events), 0);
  assert.equal(L.applyDeviceEvents(s, { e5: { type: 'uncomplete', itemId: 'a', date: d } }), 1);
  assert.equal(L.findItem(s, d, 'a').durum, 'bekliyor');
});

test('cihaz bağlı mı', () => {
  assert.equal(L.deviceOnline({ lastSeen: 1000 }, 1000 + 60000), true);
  assert.equal(L.deviceOnline({ lastSeen: 1000 }, 1000 + 5 * 60000), false);
  assert.equal(L.deviceOnline(null), false);
});

test('Firebase’den dönen bozuk şekiller düzeltilir', () => {
  freshStore();
  store.importJSON(JSON.stringify({ data: {
    settings: { dailyGoalMin: 200 },
    hours: [{ on: false, start: '09:00', end: '12:00' }],
    plans: { '2026-11-17': { not: 'x' } },
    exams: { 0: { id: 'e', tur: 'TYT', ad: 'D', tarih: '2026-11-01' } }
  } }));
  assert.deepEqual(store.state.plans['2026-11-17'].items, []);
  assert.equal(store.state.hours[0].on, false);
  assert.equal(store.state.hours[1].on, true);
  assert.equal(store.state.exams.length, 1);
  assert.deepEqual(store.state.exams[0].dersler, []);
  assert.deepEqual(store.state.tasks, []);
});

test('giriş, eşitleme ve geri yükleme uçtan uca çalışır', async () => {
  freshStore();
  const fb = fakeFirebase();
  const today = L.dateStr();
  store.update((s) => {
    s.settings.apiKey = 'sk-claude-gizli';
    s.plans[today] = { items: [{ id: 'a', saat: '10:00', ders: 'Fizik', konu: 'K', dk: 30, durum: 'bekliyor' }] };
  });

  await assert.rejects(
    connect({ dbUrl: 'x-default-rtdb.europe-west1.firebasedatabase.app', apiKey: 'AIza', email: 'a@b.c', password: 'yanlis' }),
    /E-posta ya da şifre yanlış/
  );

  await connect({ dbUrl: 'x-default-rtdb.europe-west1.firebasedatabase.app', apiKey: 'AIza', email: 'a@b.c', password: '123456', create: true });
  const user = fb.db.users.u1;
  assert.equal(user.device.view.items[0].id, 'a');
  assert.equal(user.device.view.updatedAt, 1700000000000);
  assert.ok(!JSON.stringify(user.backup).includes('sk-claude-gizli'));
  assert.ok(!JSON.stringify(user.backup).includes('ref'));
  assert.equal(store.state.settings.cloud.uid, 'u1');

  // Masa saati bir olay ve durum yazar.
  user.device.events = { k1: { type: 'session', itemId: 'a', date: today, sec: 1200, at: 5 } };
  user.device.status = { lastSeen: Date.now(), fw: 'v1' };
  await syncNow();
  assert.equal(L.findItem(store.state, today, 'a').durum, 'tamam');
  assert.equal(Object.keys(user.device.events || {}).length, 0);
  assert.equal(user.device.view.items[0].durum, 'tamam');
  assert.equal(user.device.view.studiedMin, 20);
  assert.equal(syncState.status.fw, 'v1');
  assert.equal(syncState.applied, 1);

  // Değişiklik yoksa görünüm tekrar yazılmaz.
  const before = fb.calls.filter((c) => c.startsWith('PUT') && c.includes('/view')).length;
  await syncNow();
  assert.equal(fb.calls.filter((c) => c.startsWith('PUT') && c.includes('/view')).length, before);

  // Yedek en fazla 20 saniyede bir yazılır; "Şimdi eşitle" hemen yazar.
  assert.equal(user.backup.data.plans[today].items[0].durum, 'bekliyor');
  await syncNow({ force: true });
  assert.equal(user.backup.data.plans[today].items[0].durum, 'tamam');

  // Telefon sıfırlanır, buluttan geri yüklenir; oturum ve Claude anahtarı korunur.
  store.update((s) => {
    s.plans = {};
    s.sessions = [];
  });
  await restoreFromCloud();
  assert.equal(L.findItem(store.state, today, 'a').durum, 'tamam');
  assert.equal(store.state.sessions.length, 1);
  assert.equal(store.state.settings.apiKey, 'sk-claude-gizli');
  assert.equal(store.state.settings.cloud.uid, 'u1');
});

test('süresi dolan jeton yenilenir', async () => {
  freshStore();
  fakeFirebase();
  store.update((s) => {
    s.settings.cloud = { dbUrl: 'https://x.firebasedatabase.app', apiKey: 'AIza', uid: 'u9', idToken: 'eski', refreshToken: 'ref', expiresAt: Date.now() - 1000 };
  });
  await syncNow({ force: true });
  assert.equal(store.state.settings.cloud.idToken, 'tok2');
  assert.equal(store.state.settings.cloud.refreshToken, 'ref2');
});
