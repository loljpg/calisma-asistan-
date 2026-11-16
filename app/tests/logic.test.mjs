import test from 'node:test';
import assert from 'node:assert/strict';
import * as L from '../www/js/logic.js';
import { store, defaults } from '../www/js/store.js';

test('net hesabı: 4 yanlış 1 doğruyu götürür', () => {
  assert.equal(L.net(30, 8), 28);
  assert.equal(L.net(10, 1), 9.75);
  assert.equal(L.fmtNum(L.net(10, 1)), '9,75');
  const exam = { dersler: [{ d: 30, y: 4 }, { d: 15, y: 2 }] };
  assert.equal(L.examNet(exam), 29 + 14.5);
});

test('tarih yardımcıları', () => {
  assert.equal(L.addDays('2026-11-30', 1), '2026-12-01');
  assert.equal(L.addDays('2026-03-01', -1), '2026-02-28');
  assert.equal(L.daysBetween('2026-11-16', '2026-11-20'), 4);
  assert.equal(L.longDate('2026-11-16'), 'Pazartesi, 16 Kasım');
  assert.equal(L.fmtMin(135), '2 sa 15 dk');
  assert.equal(L.fmtMin(45), '45 dk');
  assert.equal(L.fmtMin(120), '2 sa');
  assert.equal(L.fmtClock(754), '12:34');
  assert.equal(L.fmtClock(3725), '1:02:05');
});

test('seri sayacı bugün ya da dünden geriye sayar', () => {
  const s = (tarih) => ({ tarih, sec: 600 });
  assert.equal(L.streak([s('2026-11-14'), s('2026-11-15'), s('2026-11-16')], '2026-11-16'), 3);
  assert.equal(L.streak([s('2026-11-14'), s('2026-11-15')], '2026-11-16'), 2);
  assert.equal(L.streak([s('2026-11-13')], '2026-11-16'), 0);
});

test('zayıf konular: aynı konunun en son sonucu sayılır', () => {
  const tests = [
    { ders: 'Fizik', konu: 'Kuvvet', dogru: 3, toplam: 10, tarih: '2026-11-01' },
    { ders: 'Fizik', konu: 'Kuvvet', dogru: 8, toplam: 10, tarih: '2026-11-10' },
    { ders: 'Kimya', konu: 'Mol', dogru: 4, toplam: 10, tarih: '2026-11-12' }
  ];
  const weak = L.weakTopics(tests);
  assert.equal(weak.length, 1);
  assert.equal(weak[0].konu, 'Mol');
});

test('plan cevabı okunur ve doğrulanır', () => {
  const text = 'İşte plan:\n```json\n{"tarih":"2026-11-16","plan":[{"saat":"19:30","ders":"Türkçe","konu":"Paragraf","dk":40,"odev_id":null,"tasinan_id":null},{"saat":"9:15","ders":"Matematik","konu":"Fonksiyon","dk":45,"odev_id":"t1","tasinan_id":"old1"}],"not":"Deneme"}\n```';
  const p = L.parsePlan(text);
  assert.equal(p.items.length, 2);
  assert.equal(p.items[0].saat, '09:15');
  assert.equal(p.items[0].taskId, 't1');
  assert.equal(p.items[0].carriedId, 'old1');
  assert.equal(p.not, 'Deneme');
  assert.throws(() => L.parsePlan('{"plan":[{"saat":"25:00","ders":"X","dk":30}]}'), /saati hatalı/);
  assert.throws(() => L.parsePlan('{"plan":[{"saat":"10:00","ders":"X","dk":3}]}'), /süresi hatalı/);
  assert.throws(() => L.parsePlan('merhaba'), /JSON bulunamadı/);
});

test('plan uygulanınca tamamlananlar kalır, taşınanlar işaretlenir', () => {
  const state = defaults();
  state.tasks.push({ id: 't1', ders: 'Matematik', konu: 'Fonksiyon', dk: 90, done: false });
  state.plans['2026-11-15'] = { items: [{ id: 'old1', saat: '18:00', ders: 'Kimya', konu: 'Mol', dk: 30, durum: 'bekliyor' }] };
  state.plans['2026-11-16'] = { items: [
    { id: 'a', saat: '15:00', ders: 'Fizik', konu: 'K', dk: 30, durum: 'tamam' },
    { id: 'b', saat: '16:00', ders: 'Kimya', konu: 'K', dk: 30, durum: 'bekliyor' }
  ] };
  const carried = L.carryOver(state.plans, '2026-11-16');
  assert.deepEqual(carried.map((c) => c.id), ['old1']);
  const parsed = L.parsePlan('{"plan":[{"saat":"18:00","ders":"Kimya","konu":"Mol","dk":30,"tasinan_id":"old1"},{"saat":"19:00","ders":"Matematik","konu":"Fonksiyon","dk":45,"odev_id":"t1"},{"saat":"20:00","ders":"Tarih","konu":"X","dk":30,"odev_id":"yok"}],"not":""}');
  const plan = L.applyPlan(state, '2026-11-16', parsed, 'ai');
  assert.equal(plan.items.length, 4);
  assert.equal(plan.items[0].id, 'a');
  assert.equal(plan.items.find((i) => i.ders === 'Matematik').taskId, 't1');
  assert.equal(plan.items.find((i) => i.ders === 'Tarih').taskId, null);
  assert.equal(state.plans['2026-11-15'].items[0].durum, 'tasindi');

  // Kronometresi çalışan oturum yeni planda korunur.
  const activeId = plan.items.find((i) => i.ders === 'Tarih').id;
  state.timer = { itemId: activeId, startedAt: Date.now(), acc: 0 };
  const again = L.applyPlan(state, '2026-11-16', L.parsePlan('{"plan":[{"saat":"21:00","ders":"Fizik","dk":30}]}'), 'ai');
  assert.ok(again.items.some((i) => i.id === activeId));
  assert.equal(again.items.length, 3);
});

test('tamamlama oturum kaydeder, geri alma manuel kaydı siler', () => {
  const state = defaults();
  state.plans['2026-11-16'] = { items: [{ id: 'x', saat: '10:00', ders: 'Fizik', konu: 'K', dk: 30, durum: 'bekliyor', taskId: 't9' }] };
  L.completeItem(state, '2026-11-16', 'x');
  assert.equal(state.sessions.length, 1);
  assert.equal(state.sessions[0].sec, 1800);
  assert.equal(L.taskSpent(state.sessions, 't9'), 30);
  L.completeItem(state, '2026-11-16', 'x');
  assert.equal(state.sessions.length, 1);
  L.uncompleteItem(state, '2026-11-16', 'x');
  assert.equal(state.sessions.length, 0);
  assert.equal(state.plans['2026-11-16'].items[0].durum, 'bekliyor');
});

test('prompt verisi doğru alanları içerir', () => {
  const state = defaults();
  state.tasks.push({ id: 't1', ders: 'Matematik', konu: 'Türev', dk: 60, done: false, due: '2026-11-18' });
  state.tasks.push({ id: 't2', ders: 'Fizik', konu: 'Bitti', dk: 30, done: false });
  state.sessions.push({ tarih: '2026-11-15', ders: 'Fizik', sec: 1800, taskId: 't2' });
  const now = new Date(2026, 10, 16, 17, 5);
  const data = L.buildPromptData(state, '2026-11-16', 'Akşam 9’da yatacağım', now);
  assert.equal(data.gun, 'Pazartesi');
  assert.equal(data.simdi, '17:05');
  assert.equal(data.musait, '17:00-22:00');
  assert.deepEqual(data.odevler.map((o) => o.id), ['t1']);
  assert.equal(data.ogrenci_istegi, 'Akşam 9’da yatacağım');
  assert.match(L.buildUserMessage(data), /```json/);
});

test('bildirim listesi sadece gelecekteki bekleyen oturumları alır', () => {
  const state = defaults();
  state.plans['2026-11-16'] = { items: [
    { id: 'p', saat: '16:00', ders: 'A', konu: '', dk: 30, durum: 'bekliyor' },
    { id: 'q', saat: '19:00', ders: 'B', konu: 'x', dk: 30, durum: 'bekliyor' },
    { id: 'r', saat: '20:00', ders: 'C', konu: 'x', dk: 30, durum: 'tamam' }
  ] };
  const list = L.notificationsFor(state, new Date(2026, 10, 16, 17, 0), 5);
  assert.equal(list.length, 1);
  assert.equal(list[0].title, '19:00 · B');
  assert.equal(list[0].at.getHours(), 18);
  assert.equal(list[0].at.getMinutes(), 55);
});

test('yedek: API anahtarı dışa aktarılmaz, içe aktarınca korunur', () => {
  const mem = new Map();
  const storage = { getItem: (k) => mem.get(k) ?? null, setItem: (k, v) => mem.set(k, v) };
  store.load(storage);
  store.update((s) => { s.settings.apiKey = 'sk-gizli'; s.tasks.push({ id: 'z' }); });
  const json = store.exportJSON();
  assert.ok(!json.includes('sk-gizli'));
  store.update((s) => { s.tasks = []; });
  store.importJSON(json);
  assert.equal(store.state.tasks.length, 1);
  assert.equal(store.state.settings.apiKey, 'sk-gizli');
  assert.throws(() => store.importJSON('{"a":1}'), /yedek değil/);
});
