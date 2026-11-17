// Saf iş mantığı: DOM'a dokunmaz, Node'da test edilebilir.

export const DERSLER = [
  'Türkçe', 'Edebiyat', 'Matematik', 'Geometri', 'Fizik', 'Kimya', 'Biyoloji',
  'Tarih', 'Coğrafya', 'Felsefe', 'Din Kültürü', 'İngilizce', 'Diğer'
];

// Açık ve koyu temada okunur, orta doygunlukta ders renkleri (masa saati de aynılarını kullanır).
const DERS_RENK = {
  'Türkçe': '#D9508E', 'Edebiyat': '#B455C9', 'Matematik': '#2F86D1', 'Geometri': '#4B68D6',
  'Fizik': '#7357D6', 'Kimya': '#1E9C74', 'Biyoloji': '#5A9E36', 'Tarih': '#D9732B',
  'Coğrafya': '#C99415', 'Felsefe': '#8E62C9', 'Din Kültürü': '#A88536', 'İngilizce': '#1C93A8'
};
export function dersRenk(ders) {
  return DERS_RENK[ders] || '#8A8F98';
}

// Deneme bölümleri. max: bilinen soru sayısı (yoksa null, kontrol edilmez).
export const SINAV_BOLUMLERI = {
  TYT: [
    { ders: 'Türkçe', max: 40 },
    { ders: 'Sosyal Bilimler', max: 20 },
    { ders: 'Temel Matematik', max: 40 },
    { ders: 'Fen Bilimleri', max: 20 }
  ],
  AYT: [
    { ders: 'Matematik', max: 40 },
    { ders: 'Fizik', max: 14 },
    { ders: 'Kimya', max: 13 },
    { ders: 'Biyoloji', max: 13 },
    { ders: 'Edebiyat', max: 24 },
    { ders: 'Tarih-1', max: 10 },
    { ders: 'Coğrafya-1', max: 6 },
    { ders: 'Tarih-2', max: null },
    { ders: 'Coğrafya-2', max: null },
    { ders: 'Felsefe Grubu', max: null },
    { ders: 'Din Kültürü', max: null }
  ]
};

export const GUNLER = ['Pazar', 'Pazartesi', 'Salı', 'Çarşamba', 'Perşembe', 'Cuma', 'Cumartesi'];
export const GUN_KISA = ['Paz', 'Pzt', 'Sal', 'Çar', 'Per', 'Cum', 'Cmt'];
export const AYLAR = ['Ocak', 'Şubat', 'Mart', 'Nisan', 'Mayıs', 'Haziran', 'Temmuz', 'Ağustos', 'Eylül', 'Ekim', 'Kasım', 'Aralık'];

// ---------- Tarih / saat ----------
export const pad = (n) => String(n).padStart(2, '0');

export function dateStr(d = new Date()) {
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}
export function parseDate(s) {
  const [y, m, d] = s.split('-').map(Number);
  return new Date(y, m - 1, d);
}
export function addDays(s, n) {
  const d = parseDate(s);
  d.setDate(d.getDate() + n);
  return dateStr(d);
}
export function daysBetween(a, b) {
  return Math.round((parseDate(b) - parseDate(a)) / 86400000);
}
export function longDate(s) {
  const d = parseDate(s);
  return `${GUNLER[d.getDay()]}, ${d.getDate()} ${AYLAR[d.getMonth()]}`;
}
export function shortDate(s) {
  const d = parseDate(s);
  return `${d.getDate()} ${AYLAR[d.getMonth()].slice(0, 3)}`;
}
export function timeStr(d = new Date()) {
  return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
}
export function toMin(hhmm) {
  const [h, m] = String(hhmm).split(':').map(Number);
  return h * 60 + m;
}
export function atDateTime(date, hhmm) {
  const d = parseDate(date);
  const [h, m] = hhmm.split(':').map(Number);
  d.setHours(h, m, 0, 0);
  return d;
}

// ---------- Biçimlendirme ----------
export function fmtMin(m) {
  m = Math.max(0, Math.round(m));
  const h = Math.floor(m / 60);
  const r = m % 60;
  if (!h) return `${r} dk`;
  return r ? `${h} sa ${r} dk` : `${h} sa`;
}
export function fmtClock(sec) {
  sec = Math.max(0, Math.floor(sec));
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  return h ? `${h}:${pad(m)}:${pad(s)}` : `${pad(m)}:${pad(s)}`;
}
export function fmtNum(n) {
  const r = Math.round(n * 100) / 100;
  return String(r).replace('.', ',');
}
export function uid() {
  return Date.now().toString(36) + Math.random().toString(36).slice(2, 8);
}
// Bildirim kimliği için 31 bitlik pozitif sayı.
export function hashId(str) {
  let h = 0;
  for (let i = 0; i < str.length; i++) h = (Math.imul(31, h) + str.charCodeAt(i)) | 0;
  return (h & 0x7fffffff) || 1;
}

// ---------- Deneme / test ----------
export function net(d, y) {
  return (Number(d) || 0) - (Number(y) || 0) / 4;
}
export function examNet(exam) {
  return exam.dersler.reduce((a, x) => a + net(x.d, x.y), 0);
}
export function testPct(t) {
  return t.toplam ? Math.round((t.dogru / t.toplam) * 100) : 0;
}
// Her ders+konu için en son sonucu alır, %60 altındakileri zayıf sayar.
export function weakTopics(tests, limit = 6) {
  const latest = new Map();
  const sorted = [...tests].sort((a, b) => (a.tarih < b.tarih ? -1 : a.tarih > b.tarih ? 1 : 0));
  for (const t of sorted) latest.set(`${t.ders}|${t.konu}`.toLocaleLowerCase('tr'), t);
  return [...latest.values()]
    .filter((t) => testPct(t) < 60)
    .sort((a, b) => testPct(a) - testPct(b))
    .slice(0, limit);
}

// ---------- Süreler / istatistik ----------
export function minutesOn(sessions, date) {
  return sessions.filter((s) => s.tarih === date).reduce((a, s) => a + s.sec, 0) / 60;
}
export function streak(sessions, today) {
  const days = new Set(sessions.filter((s) => s.sec >= 60).map((s) => s.tarih));
  let d = days.has(today) ? today : addDays(today, -1);
  let n = 0;
  while (days.has(d)) {
    n++;
    d = addDays(d, -1);
  }
  return n;
}
export function lastDays(sessions, today, n = 7) {
  const out = [];
  for (let i = n - 1; i >= 0; i--) {
    const date = addDays(today, -i);
    out.push({ date, label: GUN_KISA[parseDate(date).getDay()], min: Math.round(minutesOn(sessions, date)) });
  }
  return out;
}
export function bySubject(sessions, fromDate, toDate) {
  const m = {};
  for (const s of sessions) {
    if (s.tarih >= fromDate && s.tarih <= toDate) m[s.ders] = (m[s.ders] || 0) + s.sec / 60;
  }
  return Object.entries(m)
    .map(([ders, min]) => ({ ders, min: Math.round(min) }))
    .filter((x) => x.min > 0)
    .sort((a, b) => b.min - a.min);
}
export function taskSpent(sessions, taskId) {
  return Math.round(sessions.filter((s) => s.taskId === taskId).reduce((a, s) => a + s.sec, 0) / 60);
}
export function examCountdown(examDate, today) {
  if (!examDate) return null;
  const n = daysBetween(today, examDate);
  return n >= 0 ? n : null;
}

// ---------- Plan ----------
export function sortItems(items) {
  return [...items].sort((a, b) => toMin(a.saat) - toMin(b.saat));
}
// Önceki günlerden tamamlanmamış oturumlar.
export function carryOver(plans, target, lookback = 7) {
  const out = [];
  for (let i = 1; i <= lookback; i++) {
    const d = addDays(target, -i);
    const p = plans[d];
    if (!p) continue;
    for (const it of p.items) {
      if (it.durum === 'bekliyor' || it.durum === 'ertelendi') out.push({ ...it, tarih: d });
    }
  }
  return out;
}

export const SYSTEM_PROMPT = `Sen YKS'ye (TYT ve AYT) hazırlanan bir lise öğrencisinin çalışma koçu ve planlayıcısısın.
Görevin, verilen verilere göre tek bir günün çalışma planını hazırlamak.

Kurallar:
- Oturumları yalnızca "musait" aralığının içine yerleştir. "simdi" verilmişse bu saatten önceye oturum koyma; ilk oturumu en erken 10 dakika sonraya koy.
- Her oturum 25-60 dakika olsun. Oturumlar arasında 10-15 dakika mola bırak; molaları plana yazma.
- Toplam süre "gunluk_hedef_dk" değerini ve müsait süreyi aşmasın.
- Öncelik sırası: son günü yaklaşan ödevler, "yarim_kalanlar", "zayif_konular", sonra diğer ödevler.
- Bir ödev için oturum koyarsan "odev_id" alanına o ödevin id'sini yaz, değilse null yaz.
- "yarim_kalanlar" listesinden plana aldığın her öğe için "tasinan_id" alanına onun id'sini yaz, değilse null yaz.
- "bugun_tamamlananlar" listesindeki işleri tekrar planlama.
- "ogrenci_istegi" varsa ona öncelik ver.
- "konu" kısa ve somut olsun (ör. "Bileşke fonksiyon – 30 soru").
- "not" alanına öğrenciye 1-2 cümlelik, Türkçe, samimi bir açıklama yaz: neyi neden öne aldığını söyle.

Cevabın SADECE şu yapıda geçerli bir JSON nesnesi olsun; başka hiçbir metin, açıklama veya kod bloğu ekleme:
{"tarih":"YYYY-MM-DD","plan":[{"saat":"HH:MM","ders":"Matematik","konu":"...","dk":45,"odev_id":null,"tasinan_id":null}],"not":"..."}`;

export function buildPromptData(state, date, request = '', now = new Date()) {
  const wd = parseDate(date).getDay();
  const h = state.hours[wd] || { on: true, start: '17:00', end: '22:00' };
  const isToday = date === dateStr(now);
  const existing = state.plans[date]?.items || [];
  const done = existing.filter((i) => i.durum === 'tamam');
  const pendingToday = existing.filter((i) => i.durum === 'bekliyor' || i.durum === 'ertelendi');
  const carried = carryOver(state.plans, date);

  const odevler = state.tasks
    .filter((t) => !t.done)
    .map((t) => {
      const kalan = t.dk ? Math.max(0, t.dk - taskSpent(state.sessions, t.id)) : null;
      return { id: t.id, ders: t.ders, konu: t.konu, aciklama: t.not || '', kalan_dk: kalan, son_gun: t.due || null };
    })
    .filter((t) => t.kalan_dk === null || t.kalan_dk > 0);

  const exams = [...state.exams].sort((a, b) => (a.tarih < b.tarih ? 1 : -1)).slice(0, 3);

  return {
    tarih: date,
    gun: GUNLER[wd],
    musait: h.on ? `${h.start}-${h.end}` : 'boş gün (öğrenci istemedikçe plan yapma)',
    simdi: isToday ? timeStr(now) : null,
    gunluk_hedef_dk: Number(state.settings.dailyGoalMin) || 300,
    sinava_kalan_gun: examCountdown(state.settings.examDate, dateStr(now)),
    bugun_tamamlananlar: done.map((i) => ({ ders: i.ders, konu: i.konu, dk: i.dk })),
    odevler,
    yarim_kalanlar: [
      ...pendingToday.map((i) => ({ id: i.id, tarih: date, ders: i.ders, konu: i.konu, dk: i.dk, odev_id: i.taskId || null })),
      ...carried.map((i) => ({ id: i.id, tarih: i.tarih, ders: i.ders, konu: i.konu, dk: i.dk, odev_id: i.taskId || null }))
    ],
    zayif_konular: weakTopics(state.tests).map((t) => ({ ders: t.ders, konu: t.konu, basari: `%${testPct(t)}`, tarih: t.tarih })),
    son_denemeler: exams.map((e) => ({
      tur: e.tur,
      ad: e.ad,
      tarih: e.tarih,
      toplam_net: Math.round(examNet(e) * 100) / 100,
      dersler: e.dersler.map((x) => ({ ders: x.ders, net: Math.round(net(x.d, x.y) * 100) / 100 }))
    })),
    ogrenci_istegi: request ? String(request).slice(0, 500) : null
  };
}

export function buildUserMessage(data) {
  return 'Plan verileri:\n```json\n' + JSON.stringify(data, null, 2) + '\n```';
}

// Claude'un cevabını okuyup doğrular. Hata varsa Türkçe mesajla Error fırlatır.
export function parsePlan(text) {
  if (!text || typeof text !== 'string' || !text.trim()) throw new Error('Cevap boş.');
  let s = text.trim();
  const fence = s.match(/```(?:json)?\s*([\s\S]*?)```/i);
  if (fence) s = fence[1];
  const a = s.indexOf('{');
  const b = s.lastIndexOf('}');
  if (a < 0 || b <= a) throw new Error('Cevapta JSON bulunamadı.');
  let obj;
  try {
    obj = JSON.parse(s.slice(a, b + 1));
  } catch {
    throw new Error('JSON okunamadı. Cevabın tamamını kopyaladığından emin ol.');
  }
  if (!Array.isArray(obj.plan)) throw new Error('JSON içinde "plan" listesi yok.');
  const items = obj.plan.map((p, i) => {
    const saat = String(p.saat || '').trim();
    if (!/^([01]?\d|2[0-3]):[0-5]\d$/.test(saat)) throw new Error(`${i + 1}. oturumun saati hatalı: "${saat}"`);
    const dk = Math.round(Number(p.dk));
    if (!Number.isFinite(dk) || dk < 5 || dk > 240) throw new Error(`${i + 1}. oturumun süresi hatalı.`);
    const ders = String(p.ders || '').trim();
    if (!ders) throw new Error(`${i + 1}. oturumda ders adı yok.`);
    return {
      saat: saat.padStart(5, '0'),
      ders: ders.slice(0, 40),
      konu: String(p.konu || '').trim().slice(0, 120),
      dk,
      taskId: p.odev_id ? String(p.odev_id) : null,
      carriedId: p.tasinan_id ? String(p.tasinan_id) : null
    };
  });
  return { items: sortItems(items), not: typeof obj.not === 'string' ? obj.not.trim().slice(0, 400) : '' };
}

// Okunan planı state'e yazar: tamamlanmış ve kronometresi çalışan oturumlar kalır,
// taşınanlar işaretlenir.
export function applyPlan(state, date, parsed, source) {
  const prev = state.plans[date];
  const activeId = state.timer?.itemId || null;
  const keep = prev ? prev.items.filter((i) => i.durum === 'tamam' || i.id === activeId) : [];
  const taskIds = new Set(state.tasks.map((t) => t.id));
  const carriedIds = new Set(parsed.items.map((p) => p.carriedId).filter(Boolean));
  const newItems = parsed.items.map((p) => ({
    id: uid(),
    saat: p.saat,
    ders: p.ders,
    konu: p.konu,
    dk: p.dk,
    durum: 'bekliyor',
    taskId: p.taskId && taskIds.has(p.taskId) ? p.taskId : null,
    fromPrev: !!p.carriedId
  }));
  for (const [d, pl] of Object.entries(state.plans)) {
    if (d >= date) continue;
    for (const it of pl.items) if (carriedIds.has(it.id) && it.durum !== 'tamam') it.durum = 'tasindi';
  }
  state.plans[date] = { items: sortItems([...keep, ...newItems]), not: parsed.not, source, createdAt: Date.now() };
  return state.plans[date];
}

export function findItem(state, date, itemId) {
  return state.plans[date]?.items.find((i) => i.id === itemId) || null;
}

export function completeItem(state, date, itemId, sec = null, kaynak = 'manuel') {
  const it = findItem(state, date, itemId);
  if (!it) return;
  it.durum = 'tamam';
  if (!state.sessions.some((s) => s.itemId === itemId)) {
    state.sessions.push({
      id: uid(), tarih: date, ders: it.ders, konu: it.konu, sec: sec ?? it.dk * 60,
      itemId, taskId: it.taskId || null, kaynak, end: Date.now()
    });
  }
}

export function uncompleteItem(state, date, itemId) {
  const it = findItem(state, date, itemId);
  if (!it) return;
  it.durum = 'bekliyor';
  state.sessions = state.sessions.filter((s) => !(s.itemId === itemId && s.kaynak === 'manuel'));
}

// ---------- Kronometre ----------
export function timerElapsedMs(t, now = Date.now()) {
  if (!t) return 0;
  return (t.acc || 0) + (t.startedAt ? now - t.startedAt : 0);
}

// ---------- Masa saati (ESP32) ----------
// Cihazın göstereceği bugünün özeti. Firebase'de users/{uid}/device/view altına yazılır.
export function deviceView(state, now = new Date()) {
  const today = dateStr(now);
  const items = (state.plans[today]?.items || [])
    .filter((i) => i.durum !== 'tasindi')
    .map((i) => ({ id: i.id, saat: i.saat, ders: i.ders, konu: i.konu || '', dk: i.dk, durum: i.durum }));
  return {
    date: today,
    items,
    studiedMin: Math.round(minutesOn(state.sessions, today)),
    goalMin: Number(state.settings.dailyGoalMin) || 300,
    streak: streak(state.sessions, today),
    examDays: examCountdown(state.settings.examDate, today)
  };
}

// Cihazdan gelen olayları (tamamlama, geri alma, kronometre oturumu) uygular.
// Aynı olay iki kez gelirse ikinci kez uygulanmaz. Uygulanan olay sayısını döndürür.
export function applyDeviceEvents(state, events, now = new Date()) {
  if (!Array.isArray(state.processedEvents)) state.processedEvents = [];
  const seen = new Set(state.processedEvents);
  const list = Object.entries(events || {})
    .filter(([key, e]) => e && typeof e === 'object' && !seen.has(key))
    .sort((a, b) => (Number(a[1].at) || 0) - (Number(b[1].at) || 0));
  let applied = 0;
  for (const [key, e] of list) {
    const date = /^\d{4}-\d{2}-\d{2}$/.test(String(e.date)) ? e.date : dateStr(now);
    const it = e.itemId ? findItem(state, date, String(e.itemId)) : null;
    if (e.type === 'complete' && it && it.durum !== 'tamam') {
      completeItem(state, date, it.id);
      applied++;
    } else if (e.type === 'uncomplete' && it && it.durum === 'tamam') {
      uncompleteItem(state, date, it.id);
      applied++;
    } else if (e.type === 'session') {
      const sec = Math.round(Number(e.sec));
      if (sec >= 60 && sec <= 6 * 3600) {
        state.sessions.push({
          id: uid(), tarih: date,
          ders: String(it?.ders || e.ders || 'Diğer').slice(0, 40),
          konu: String(it?.konu || e.konu || '').slice(0, 120),
          sec, itemId: it ? it.id : null, taskId: it?.taskId || null, kaynak: 'cihaz',
          end: Number(e.at) || Date.now()
        });
        if (it) it.durum = 'tamam';
        applied++;
      }
    }
    state.processedEvents.push(key);
  }
  state.processedEvents = state.processedEvents.slice(-300);
  return applied;
}

// Cihaz son 3 dakika içinde haber verdiyse bağlı sayılır.
export function deviceOnline(status, now = Date.now()) {
  const t = Number(status?.lastSeen);
  return Number.isFinite(t) && t > 0 && now - t < 3 * 60 * 1000;
}

export function agoText(ms, now = Date.now()) {
  const d = Math.max(0, Math.round((now - ms) / 1000));
  if (d < 60) return 'az önce';
  if (d < 3600) return `${Math.floor(d / 60)} dk önce`;
  if (d < 86400) return `${Math.floor(d / 3600)} sa önce`;
  return `${Math.floor(d / 86400)} gün önce`;
}

// ---------- Bildirimler ----------
export function notificationsFor(state, now = new Date(), beforeMin = 5) {
  const out = [];
  const today = dateStr(now);
  for (const date of [today, addDays(today, 1)]) {
    for (const it of state.plans[date]?.items || []) {
      if (it.durum !== 'bekliyor') continue;
      const at = new Date(atDateTime(date, it.saat).getTime() - beforeMin * 60000);
      if (at <= now) continue;
      out.push({
        id: hashId(it.id),
        at,
        title: `${it.saat} · ${it.ders}`,
        body: `${it.konu || 'Çalışma zamanı'} · ${it.dk} dk`
      });
    }
  }
  return out;
}
