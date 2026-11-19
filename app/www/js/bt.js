// Masa saatiyle Bluetooth köprüsü. Bağlantıyı Android tarafındaki servis (plugins/masa-saati)
// tutar; uygulama kapalıyken bile masa saatinden gelen olaylar orada birikir. Bu dosya:
// - günün planını ve kronometreyi servise verir (bağlanınca masa saatine gider),
// - masa saatinden gelen tikleri, ertelemeleri, oturumları ve kronometreyi uygular.
import { store } from './store.js';
import * as L from './logic.js';
import { btPlugin } from './native.js';
import { nextTimerRev, relayDeviceTimer } from './sync.js';

export const bt = { available: false, state: null };

let lastViewSig = '';
let lastTimerSig = null;
let applyingRemote = false;
let onChange = null;

export async function initBt(changed) {
  const p = btPlugin();
  if (!p) return;
  bt.available = true;
  onChange = changed;
  p.addListener('state', (s) => {
    const wasAuthed = bt.state?.authed;
    bt.state = s;
    if (s?.authed && !wasAuthed) {
      // Yeni bağlantı: servis kayıtlı son hâli zaten gönderdi, değiştiyse yenisi gider.
      pushBt(true);
    }
    onChange?.();
  });
  p.addListener('pending', () => {
    takePending();
  });
  try {
    bt.state = await p.getState();
  } catch {
    bt.state = null;
  }
  await takePending();
  pushBt(true);
  onChange?.();
}

export async function refreshBt() {
  const p = btPlugin();
  if (!p) return;
  try {
    bt.state = await p.getState();
  } catch {
    /* önemli değil */
  }
  await takePending();
}

let taking = false;
export async function takePending() {
  const p = btPlugin();
  if (!p || taking) return;
  taking = true;
  try {
    const r = await p.takePending();
    const events = r?.events && typeof r.events === 'object' ? r.events : {};
    if (Object.keys(events).length) {
      store.update((s) => {
        L.applyDeviceEvents(s, events);
      });
    }
    if (r?.timer) {
      applyingRemote = true;
      try {
        relayDeviceTimer(r.timer);
      } finally {
        applyingRemote = false;
      }
      lastTimerSig = L.timerSig(store.state.timer);
    }
  } catch (e) {
    console.warn('Bluetooth', e);
  } finally {
    taking = false;
  }
}

// Plan ya da kronometre değiştiyse servise verir. Servis kaydı saklar, bağlıysa hemen gönderir.
export function pushBt(force = false) {
  const p = btPlugin();
  if (!p || !bt.state?.paired) return;
  const view = L.deviceView(store.state);
  const sig = JSON.stringify(view);
  if (force || sig !== lastViewSig) {
    lastViewSig = sig;
    p.setView({ data: { ...view, updatedAt: Date.now() } }).catch(() => {});
  }
  const tsig = L.timerSig(store.state.timer);
  if (applyingRemote) {
    lastTimerSig = tsig;
    return;
  }
  if (force ? lastTimerSig === null : tsig !== lastTimerSig) {
    lastTimerSig = tsig;
    p.setTimer({ data: L.sharedTimer(store.state, 'phone', nextTimerRev()) }).catch(() => {});
  }
}

// Ayarlar ekranı için kısa durum yazısı.
export function btText() {
  const s = bt.state;
  if (!s) return '';
  if (!s.supported) return 'Bu telefonda Bluetooth yok';
  if (!s.paired) return 'Eşleşmedi';
  if (!s.enabled) return 'Telefonun Bluetooth’u kapalı';
  if (!s.permission) return 'Bluetooth izni gerekli';
  if (s.authed) {
    const fw = s.device?.fw ? ` · ${s.device.fw}` : '';
    return `Bağlı${fw}`;
  }
  if (s.error) return s.error;
  if (s.connected) return 'Bağlanıyor…';
  return 'Masa saati bekleniyor';
}

export const btActions = {
  async requestPermission() {
    const p = btPlugin();
    const r = await p.requestBluetooth();
    if (!r?.granted) throw new Error('Bluetooth izni verilmedi. Telefonun ayarlarından “Yakındaki cihazlar” iznini aç.');
    await refreshBt();
  },

  async scan() {
    const p = btPlugin();
    const r = await p.scan();
    return (r?.devices || []).sort((a, b) => (b.rssi || -200) - (a.rssi || -200));
  },

  async pair(address, name) {
    const p = btPlugin();
    bt.state = await p.pair({ address, name });
    lastViewSig = '';
    lastTimerSig = null;
    pushBt(true);
  },

  async unpair() {
    await btPlugin().unpair();
    await refreshBt();
  },

  async setOptions(opts) {
    bt.state = await btPlugin().setOptions(opts);
  },

  async listApps() {
    const r = await btPlugin().listApps();
    return r?.apps || [];
  },

  openNotificationAccess: () => btPlugin().openNotificationAccess(),
  openBluetoothSettings: () => btPlugin().openBluetoothSettings()
};
