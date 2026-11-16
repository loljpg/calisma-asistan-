// Telefonun yerel özellikleri: bildirimler, sistem çubukları, güncelleme kontrolü.
// Tarayıcıda (önizleme) bu özellikler sessizce devre dışı kalır.
import { BUILD } from './version.js';

export { BUILD };
export const REPO = 'loljpg/calisma-asistan-';

const capExports = () => window.capacitorExports;
export const isNative = () => !!window.Capacitor?.isNativePlatform?.();

let notifPlugin = null;
function notifications() {
  if (notifPlugin) return notifPlugin;
  const register = capExports()?.registerPlugin;
  if (!register || !isNative()) return null;
  notifPlugin = register('LocalNotifications');
  return notifPlugin;
}

let appPlugin = null;
// Android geri tuşu. handler false döndürürse uygulama kapanır.
export function onBackButton(handler) {
  const register = capExports()?.registerPlugin;
  if (!register || !isNative()) return;
  appPlugin = register('App');
  appPlugin.addListener('backButton', () => {
    if (handler() === false) appPlugin.exitApp();
  });
}

export async function initSystemBars() {
  try {
    const { SystemBars, SystemBarsStyle } = capExports() || {};
    if (isNative() && SystemBars && SystemBarsStyle) await SystemBars.setStyle({ style: SystemBarsStyle.Dark });
  } catch {
    /* önemli değil */
  }
}

export async function ensureNotifPermission() {
  const p = notifications();
  if (!p) return false;
  try {
    let r = await p.checkPermissions();
    if (r.display !== 'granted') r = await p.requestPermissions();
    return r.display === 'granted';
  } catch {
    return false;
  }
}

// Eski bildirimleri iptal edip yenilerini kurar; kurulan kimlikleri döndürür.
export async function scheduleNotifications(list, oldIds = []) {
  const p = notifications();
  if (!p) return [];
  try {
    if (oldIds.length) await p.cancel({ notifications: oldIds.map((id) => ({ id })) });
    if (!list.length) return [];
    if (!(await ensureNotifPermission())) return [];
    await p.schedule({
      notifications: list.map((n) => ({
        id: n.id,
        title: n.title,
        body: n.body,
        schedule: { at: n.at, allowWhileIdle: true }
      }))
    });
    return list.map((n) => n.id);
  } catch (e) {
    console.warn('Bildirim kurulamadı', e);
    return [];
  }
}

export async function checkUpdate() {
  const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
    headers: { accept: 'application/vnd.github+json' }
  });
  if (!res.ok) throw new Error('Güncelleme bilgisi alınamadı.');
  const r = await res.json();
  const latest = parseInt(String(r.tag_name || '').replace(/^v/, ''), 10);
  const asset = (r.assets || []).find((a) => a.name.endsWith('.apk'));
  return {
    latest,
    current: BUILD,
    newer: Number.isFinite(latest) && BUILD > 0 && latest > BUILD,
    url: asset?.browser_download_url || r.html_url
  };
}
