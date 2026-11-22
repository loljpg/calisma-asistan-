// Uygulama durumu ve telefonda kalıcı saklama.

const KEY = 'calisma-asistani:v1';

export function defaults() {
  const weekday = { on: true, start: '17:00', end: '22:00' };
  const weekend = { on: true, start: '10:00', end: '20:00' };
  return {
    v: 1,
    settings: {
      apiKey: '',
      model: 'claude-haiku-5-5',
      aiMode: 'auto',
      examDate: '',
      dailyGoalMin: 300,
      theme: 'auto',
      notifications: true,
      notifyBefore: 5,
      lastUpdateCheck: 0,
      // Firebase: { dbUrl, apiKey, email, uid, idToken, refreshToken, expiresAt }
      cloud: null
    },
    hours: { 0: { ...weekend }, 1: { ...weekday }, 2: { ...weekday }, 3: { ...weekday }, 4: { ...weekday }, 5: { ...weekday }, 6: { ...weekend } },
    tasks: [],
    exams: [],
    tests: [],
    plans: {},
    sessions: [],
    // Kartın soru sayacından: { tarih, ders, n } (gün ve derse göre toplam, son 120 gün)
    questions: [],
    timer: null,
    notifIds: [],
    processedEvents: []
  };
}

// Firebase boş dizileri silebilir, ardışık sayısal anahtarlı nesneleri diziye çevirebilir.
// Buradan geçen her veri uygulamanın beklediği şekle getirilir.
const asArray = (v) => (Array.isArray(v) ? v.filter((x) => x != null) : v && typeof v === 'object' ? Object.values(v) : []);

function migrate(s) {
  const d = defaults();
  const out = {
    ...d,
    ...s,
    settings: { ...d.settings, ...(s.settings || {}) },
    hours: { ...d.hours }
  };
  const hours = s.hours || {};
  for (let i = 0; i < 7; i++) if (hours[i] && typeof hours[i] === 'object') out.hours[i] = { ...d.hours[i], ...hours[i] };
  for (const k of ['tasks', 'exams', 'tests', 'sessions', 'questions', 'notifIds', 'processedEvents']) out[k] = asArray(s[k]);
  out.exams = out.exams.map((e) => ({ ...e, dersler: asArray(e.dersler) }));
  out.plans = {};
  for (const [date, p] of Object.entries(s.plans || {})) {
    if (p && typeof p === 'object') out.plans[date] = { not: '', ...p, items: asArray(p.items) };
  }
  if (!out.timer || typeof out.timer !== 'object') out.timer = null;
  return out;
}

export const store = {
  state: defaults(),
  storage: null,
  listeners: new Set(),

  load(storage = globalThis.localStorage) {
    this.storage = storage || null;
    try {
      const raw = this.storage?.getItem(KEY);
      this.state = raw ? migrate(JSON.parse(raw)) : defaults();
    } catch {
      this.state = defaults();
    }
    return this.state;
  },

  save() {
    try {
      this.storage?.setItem(KEY, JSON.stringify(this.state));
    } catch (e) {
      console.error('Kaydedilemedi', e);
    }
  },

  update(fn) {
    fn(this.state);
    this.save();
    for (const l of this.listeners) l(this.state);
  },

  subscribe(fn) {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  },

  // Yedeklenecek veri: API anahtarı ve bulut oturum bilgileri hariç.
  backupData() {
    const data = JSON.parse(JSON.stringify(this.state));
    data.settings.apiKey = '';
    delete data.settings.cloud;
    delete data.notifIds;
    return data;
  },

  exportJSON() {
    return JSON.stringify({ app: 'calisma-asistani', exportedAt: new Date().toISOString(), data: this.backupData() });
  },

  importJSON(text) {
    let o;
    try {
      o = JSON.parse(text);
    } catch {
      throw new Error('Yedek okunamadı. Metnin tamamını yapıştırdığından emin ol.');
    }
    const data = o && o.data ? o.data : o;
    if (!data || typeof data !== 'object' || !data.settings) throw new Error('Bu metin bir yedek değil.');
    const keep = { apiKey: this.state.settings.apiKey, cloud: this.state.settings.cloud, notifIds: this.state.notifIds };
    this.state = migrate(data);
    if (!this.state.settings.apiKey) this.state.settings.apiKey = keep.apiKey;
    this.state.settings.cloud = keep.cloud;
    this.state.notifIds = keep.notifIds || [];
    this.save();
    for (const l of this.listeners) l(this.state);
  }
};
