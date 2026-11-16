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
      reduceEffects: false,
      accent: '#7DD3FC',
      notifications: true,
      notifyBefore: 5,
      lastUpdateCheck: 0
    },
    hours: { 0: { ...weekend }, 1: { ...weekday }, 2: { ...weekday }, 3: { ...weekday }, 4: { ...weekday }, 5: { ...weekday }, 6: { ...weekend } },
    tasks: [],
    exams: [],
    tests: [],
    plans: {},
    sessions: [],
    timer: null,
    notifIds: []
  };
}

function migrate(s) {
  const d = defaults();
  return {
    ...d,
    ...s,
    settings: { ...d.settings, ...(s.settings || {}) },
    hours: { ...d.hours, ...(s.hours || {}) }
  };
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

  // API anahtarı yedeğe yazılmaz.
  exportJSON() {
    const data = JSON.parse(JSON.stringify(this.state));
    data.settings.apiKey = '';
    return JSON.stringify({ app: 'calisma-asistani', exportedAt: new Date().toISOString(), data });
  },

  importJSON(text) {
    let o;
    try {
      o = JSON.parse(text);
    } catch {
      throw new Error('Yedek okunamadı. Metnin tamamını yapıştırdığından emin ol.');
    }
    const data = o && o.data ? o.data : o;
    if (!data || typeof data !== 'object' || !data.plans || !data.settings) throw new Error('Bu metin bir yedek değil.');
    const apiKey = this.state.settings.apiKey;
    this.state = migrate(data);
    if (!this.state.settings.apiKey) this.state.settings.apiKey = apiKey;
    this.save();
    for (const l of this.listeners) l(this.state);
  }
};
