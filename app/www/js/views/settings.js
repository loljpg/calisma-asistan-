// Ayarlar ekranı.
import { store } from '../store.js';
import { icon, esc, radio, openSheet, closeSheet, sheetHead, toast, copyText } from '../ui.js';
import { askClaude, MODELS } from '../ai.js';
import { BUILD, ensureNotifPermission } from '../native.js';
import * as L from '../logic.js';
import { syncState, isSignedIn, connect, disconnect, restoreFromCloud } from '../sync.js';

const THEMES = [
  ['auto', 'Otomatik'],
  ['light', 'Açık'],
  ['dark', 'Koyu']
];

function syncText() {
  if (syncState.error) return `Hata: ${syncState.error}`;
  return syncState.lastSync ? L.agoText(syncState.lastSync) : 'Henüz eşitlenmedi';
}

function deviceText() {
  const st = syncState.status;
  if (!st?.lastSeen) return 'Henüz bağlanmadı';
  const when = L.agoText(Number(st.lastSeen));
  return `${L.deviceOnline(st) ? 'Bağlı' : 'Çevrimdışı'} · ${when}${st.fw ? ` · ${st.fw}` : ''}`;
}

// Eşitlemeden sonra sadece durum yazılarını günceller (app.js çağırır).
export function refreshSyncInfo() {
  const a = document.querySelector('[data-sync-status]');
  const b = document.querySelector('[data-device-status]');
  if (a) a.textContent = syncText();
  if (b) b.textContent = deviceText();
}

function cloudSection(app) {
  const c = app.s.settings.cloud || {};
  if (!isSignedIn()) {
    return `
  <h2 class="set-title">Bulut ve masa saati</h2>
  <form class="set-group surface form" data-form="settings:cloudLogin" style="padding:16px">
    <p class="hint">Firebase’i kurduktan sonra bilgileri gir. Masa saati de aynı e-posta ve şifreyle bağlanacak. Verilerin ayrıca buluta yedeklenir.</p>
    <label class="field">Veritabanı adresi<input class="input" name="dbUrl" inputmode="url" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="https://…firebasedatabase.app" value="${esc(c.dbUrl || '')}" required></label>
    <label class="field">Web API anahtarı<input class="input" name="apiKey" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="AIza…" value="${esc(c.apiKey || '')}" required></label>
    <label class="field">E-posta<input class="input" name="email" type="email" autocomplete="username" autocapitalize="off" value="${esc(c.email || '')}" required></label>
    <label class="field">Şifre<input class="input" name="password" type="password" autocomplete="current-password" minlength="6" required></label>
    <div class="btn-row">
      <button class="btn surface" type="submit" name="mode" value="create">Hesap oluştur</button>
      <button class="btn primary" type="submit" name="mode" value="login">Giriş yap</button>
    </div>
  </form>`;
  }
  return `
  <h2 class="set-title">Bulut ve masa saati</h2>
  <div class="set-group surface">
    <div class="set-row"><span class="lbl">Hesap</span><span class="muted small">${esc(c.email)}</span></div>
    <div class="set-row"><span class="lbl">Son eşitleme</span><span class="muted small" data-sync-status>${esc(syncText())}</span></div>
    <div class="set-row"><span class="lbl">Masa saati</span><span class="muted small" data-device-status>${esc(deviceText())}</span></div>
    <div class="set-row"><span class="lbl">Eşitle</span><button class="btn surface" data-action="settings:syncNow">${icon('refresh', 16)} Şimdi</button></div>
    <div class="set-row"><span class="lbl">Buluttan geri yükle</span><button class="btn surface" data-action="settings:restoreSheet">${icon('download', 16)} Yükle</button></div>
    <div class="set-row"><span class="lbl">Bu telefonda</span><button class="btn danger" data-action="settings:cloudLogout">Çıkış yap</button></div>
  </div>`;
}


export function render(app) {
  const st = app.s.settings;
  return `
  <header class="back-head">
    <button class="icon-btn surface" data-action="ui:go" data-route="today" aria-label="Geri">${icon('back')}</button>
    <h1>Ayarlar</h1>
  </header>

  <h2 class="set-title">Claude</h2>
  <div class="set-group surface">
    <div class="set-row stack">
      <span class="lbl">Plan modu</span>
      <div class="radio-row">${radio('aiMode', 'auto', 'Otomatik', st.aiMode === 'auto', 'data-setting="aiMode"')}${radio('aiMode', 'manual', 'Manuel', st.aiMode === 'manual', 'data-setting="aiMode"')}</div>
      <p class="hint">Otomatik mod API anahtarıyla tek dokunuşta plan yapar. Manuel modda istek metnini Claude uygulamasına kendin yapıştırırsın; ek ücret yok.</p>
    </div>
    <div class="set-row stack">
      <label for="apiKey">API anahtarı</label>
      <input class="input" id="apiKey" type="password" autocomplete="off" spellcheck="false" placeholder="sk-ant-…" value="${esc(st.apiKey)}" data-setting="apiKey">
      <p class="hint">Anahtarı console.anthropic.com adresinden alırsın. Sadece bu telefonda saklanır, yedeğe yazılmaz.</p>
    </div>
    <div class="set-row">
      <label for="model">Model</label>
      <select class="input" id="model" data-setting="model">${MODELS.map((m) => `<option value="${m.id}"${m.id === st.model ? ' selected' : ''}>${m.name}</option>`).join('')}</select>
    </div>
    <div class="set-row"><span class="lbl">Bağlantı</span><button class="btn surface" data-action="settings:testKey">Test et</button></div>
  </div>

  <h2 class="set-title">Hedefler</h2>
  <div class="set-group surface">
    <div class="set-row"><label for="examDate">Sınav tarihi</label><input class="input" id="examDate" type="date" value="${esc(st.examDate)}" data-setting="examDate"></div>
    <div class="set-row"><label for="goal">Günlük hedef (dk)</label><input class="input" id="goal" type="number" inputmode="numeric" min="30" max="900" step="15" value="${st.dailyGoalMin}" data-setting="dailyGoalMin"></div>
  </div>

  <h2 class="set-title">Görünüm</h2>
  <div class="set-group surface">
    <div class="set-row stack">
      <span class="lbl" id="theme-lbl">Tema</span>
      <div class="radio-row" role="radiogroup" aria-labelledby="theme-lbl">
        ${THEMES.map(([v, n]) => radio('theme', v, n, (st.theme || 'auto') === v, 'data-setting="theme"')).join('')}
      </div>
      <p class="hint">Otomatik, telefonun açık/koyu ayarını izler.</p>
    </div>
  </div>

  <h2 class="set-title">Bildirimler</h2>
  <div class="set-group surface">
    <div class="set-row"><label for="notif">Oturum hatırlatmaları</label><input class="toggle" id="notif" type="checkbox" ${st.notifications ? 'checked' : ''} data-setting="notifications"></div>
    <div class="set-row">
      <label for="before">Ne zaman</label>
      <select class="input" id="before" data-setting="notifyBefore">${[0, 5, 10, 15].map((v) => `<option value="${v}"${Number(st.notifyBefore) === v ? ' selected' : ''}>${v ? `${v} dk önce` : 'Tam zamanında'}</option>`).join('')}</select>
    </div>
  </div>

  ${cloudSection(app)}

  <h2 class="set-title">Veri</h2>
  <div class="set-group surface">
    <div class="set-row"><span class="lbl">Yedeği kopyala</span><button class="btn surface" data-action="settings:exportData">${icon('copy', 16)} Kopyala</button></div>
    <div class="set-row"><span class="lbl">Yedekten geri yükle</span><button class="btn surface" data-action="settings:importSheet">${icon('download', 16)} Yükle</button></div>
  </div>

  <h2 class="set-title">Uygulama</h2>
  <div class="set-group surface">
    <div class="set-row"><span class="lbl">Sürüm</span><span class="muted">${BUILD ? `v${BUILD}` : 'Geliştirme'}</span></div>
    <div class="set-row"><span class="lbl">Güncellemeler</span><button class="btn surface" data-action="settings:checkUpdate">${icon('refresh', 16)} Denetle</button></div>
  </div>`;
}

// data-setting alanlarından gelen değişiklikleri kaydeder (app.js çağırır).
export function applySetting(el, app) {
  const key = el.dataset.setting;
  let v;
  if (el.type === 'checkbox') v = el.checked;
  else if (el.type === 'number' || key === 'notifyBefore') v = Number(el.value);
  else v = String(el.value).trim();
  if (key === 'dailyGoalMin' && !(v >= 30 && v <= 900)) {
    toast('Günlük hedef 30 ile 900 dakika arasında olmalı.', 'error');
    return;
  }
  app.update((s) => {
    s.settings[key] = v;
  });
  if (key === 'apiKey' && v) toast('API anahtarı kaydedildi.');
  if (key === 'notifications' && v) ensureNotifPermission();
}

export const actions = {
  async testKey(el, app) {
    const st = app.s.settings;
    if (!st.apiKey) throw new Error('Önce API anahtarını gir.');
    el.disabled = true;
    el.innerHTML = '<span class="spinner"></span>';
    try {
      await askClaude({ apiKey: st.apiKey, model: st.model, system: 'Kısa cevap ver.', user: 'Sadece "tamam" yaz.', maxTokens: 32 });
      toast('Bağlantı çalışıyor.');
    } finally {
      el.disabled = false;
      el.textContent = 'Test et';
    }
  },

  async exportData() {
    const ok = await copyText(store.exportJSON());
    toast(ok ? 'Yedek panoya kopyalandı. Bir not uygulamasına yapıştırıp sakla.' : 'Kopyalanamadı.', ok ? 'info' : 'error', 4000);
  },

  importSheet() {
    openSheet(
      `${sheetHead('Yedekten geri yükle')}
      <form class="form" data-form="settings:importData">
        <p class="hint">Daha önce kopyaladığın yedek metnini yapıştır. Bu telefondaki mevcut verilerin yerine geçer.</p>
        <textarea class="input mono" name="yedek" required aria-label="Yedek metni"></textarea>
        <button class="btn primary block" type="submit">Geri yükle</button>
      </form>`,
      'Yedekten geri yükle'
    );
  },

  async syncNow(el, app) {
    el.disabled = true;
    try {
      await app.syncNow({ force: true });
      toast(syncState.applied ? `Eşitlendi. Masa saatinden ${syncState.applied} kayıt alındı.` : 'Eşitlendi.');
    } finally {
      el.disabled = false;
    }
  },

  restoreSheet() {
    openSheet(
      `${sheetHead('Buluttan geri yükle')}
      <p class="hint" style="margin-bottom:16px">Buluttaki son yedek bu telefondaki verilerin yerine geçer. Yeni bir telefona geçtiysen ya da uygulamayı yeniden kurduysan kullan.</p>
      <div class="btn-row">
        <button class="btn surface" data-action="ui:closeSheet">Vazgeç</button>
        <button class="btn primary" data-action="settings:cloudRestore">${icon('download', 16)} Geri yükle</button>
      </div>`,
      'Buluttan geri yükle'
    );
  },

  async cloudRestore(el) {
    el.disabled = true;
    try {
      await restoreFromCloud();
      closeSheet();
      toast('Buluttaki yedek yüklendi.');
    } finally {
      el.disabled = false;
    }
  },

  cloudLogout(el, app) {
    disconnect();
    app.render();
    toast('Bu telefonda buluttan çıkış yapıldı. Verilerin telefonda duruyor.');
  },

  async checkUpdate(el, app) {
    el.disabled = true;
    try {
      const info = await app.checkUpdate(true);
      if (!info) return;
      if (info.newer) {
        toast(`Yeni sürüm var: v${info.latest}. Bugün ekranından indirebilirsin.`);
      } else {
        toast(BUILD ? 'En güncel sürümü kullanıyorsun.' : `Son yayınlanan sürüm: v${info.latest}`);
      }
    } finally {
      el.disabled = false;
    }
  }
};

export const forms = {
  async cloudLogin(data, form, app) {
    const buttons = [...form.querySelectorAll('button')];
    buttons.forEach((b) => {
      b.disabled = true;
    });
    try {
      await connect({ ...data, create: data.mode === 'create' });
      app.render();
      toast(data.mode === 'create' ? 'Hesap oluşturuldu ve buluta bağlandı.' : 'Buluta bağlandı.');
    } finally {
      buttons.forEach((b) => {
        b.disabled = false;
      });
    }
  },

  importData(data) {
    store.importJSON(data.yedek || '');
    closeSheet();
    toast('Yedek geri yüklendi.');
  }
};
