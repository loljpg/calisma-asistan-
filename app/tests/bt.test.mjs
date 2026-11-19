import test from 'node:test';
import assert from 'node:assert/strict';

// Sahte Capacitor ve MasaSaati eklentisi
const calls = [];
const listeners = {};
let pending = { events: {}, timer: null };
const plugin = {
  addListener: (name, fn) => { listeners[name] = fn; },
  getState: async () => ({ supported: true, enabled: true, permission: true, paired: true, authed: true }),
  takePending: async () => { const r = pending; pending = { events: {} }; return r; },
  setView: async (a) => { calls.push(['view', a.data]); },
  setTimer: async (a) => { calls.push(['timer', a.data]); }
};
globalThis.window = { Capacitor: { isNativePlatform: () => true }, capacitorExports: { registerPlugin: () => plugin } };

const { store } = await import('../www/js/store.js');
const L = await import('../www/js/logic.js');
const { initBt, pushBt, takePending, btText } = await import('../www/js/bt.js');

test('Bluetooth: plan ve kronometre gider, masa saatinden gelenler uygulanır', async () => {
  const mem = new Map();
  store.load({ getItem: (k) => mem.get(k) ?? null, setItem: (k, v) => mem.set(k, v) });
  const today = L.dateStr();
  store.update((s) => {
    s.plans[today] = { items: [{ id: 'a', saat: '10:00', ders: 'Fizik', konu: 'K', dk: 30, durum: 'bekliyor' }] };
  });
  await initBt(() => {});
  assert.equal(btText(), 'Bağlı');
  const view = calls.find((c) => c[0] === 'view');
  assert.equal(view[1].items[0].id, 'a');
  assert.ok(view[1].updatedAt > 0);
  assert.equal(calls.find((c) => c[0] === 'timer')[1].on, false);

  // Telefonda kronometre başladı → masa saatine gider
  calls.length = 0;
  store.update((s) => {
    s.timer = { itemId: 'a', date: today, ders: 'Fizik', konu: 'K', taskId: null, startedAt: Date.now(), acc: 0 };
  });
  pushBt();
  const t = calls.find((c) => c[0] === 'timer');
  assert.equal(t[1].on, true);
  assert.equal(t[1].by, 'phone');

  // Masa saatinden tik ve duraklatma geldi
  calls.length = 0;
  pending = {
    events: { c1: { type: 'complete', itemId: 'a', date: today, at: 1 } },
    timer: { ...t[1], startedAt: 0, acc: 120000, by: 'device', rev: t[1].rev + 10 }
  };
  await takePending();
  assert.equal(L.findItem(store.state, today, 'a').durum, 'tamam');
  assert.equal(store.state.timer.acc, 120000);
  assert.equal(store.state.timer.startedAt, null);
  // Masa saatinden gelen kronometre ona geri gönderilmez
  pushBt();
  assert.equal(calls.filter((c) => c[0] === 'timer').length, 0);
});
