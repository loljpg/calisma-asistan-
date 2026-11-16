// Ayarlar ekranı.
import { store } from '../store.js';
import { icon, esc, radio, openSheet, closeSheet, sheetHead, toast, copyText } from '../ui.js';
import { askClaude, MODELS } from '../ai.js';
import { BUILD, ensureNotifPermission } from '../native.js';

const ACCENTS = [
  ['#7DD3FC', 'Mavi'],
  ['#A5B4FC', 'Lila'],
  ['#6EE7B7', 'Yeşil'],
  ['#FDBA74', 'Turuncu']
];

export function render(app) {
  const st = app.s.settings;
  return `
  <header class="back-head">
    <button class="icon-btn glass" data-action="ui:go" data-route="today" aria-label="Geri">${icon('back')}</button>
    <h1>Ayarlar</h1>
  </header>

  <h2 class="set-title">Claude</h2>
  <div class="set-group glass">
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
    <div class="set-row"><span class="lbl">Bağlantı</span><button class="btn glass" data-action="settings:testKey">Test et</button></div>
  </div>

  <h2 class="set-title">Hedefler</h2>
  <div class="set-group glass">
    <div class="set-row"><label for="examDate">Sınav tarihi</label><input class="input" id="examDate" type="date" value="${esc(st.examDate)}" data-setting="examDate"></div>
    <div class="set-row"><label for="goal">Günlük hedef (dk)</label><input class="input" id="goal" type="number" inputmode="numeric" min="30" max="900" step="15" value="${st.dailyGoalMin}" data-setting="dailyGoalMin"></div>
  </div>

  <h2 class="set-title">Görünüm</h2>
  <div class="set-group glass">
    <div class="set-row"><label for="reduce">Efektleri azalt</label><input class="toggle" id="reduce" type="checkbox" ${st.reduceEffects ? 'checked' : ''} data-setting="reduceEffects"></div>
    <div class="set-row">
      <span class="lbl" id="accent-lbl">Vurgu rengi</span>
      <div class="swatches" role="radiogroup" aria-labelledby="accent-lbl">
        ${ACCENTS.map(([c, n]) => `<label class="swatch" style="--c:${c}"><input type="radio" name="accent" value="${c}" aria-label="${n}" ${st.accent === c ? 'checked' : ''} data-setting="accent"></label>`).join('')}
      </div>
    </div>
  </div>

  <h2 class="set-title">Bildirimler</h2>
  <div class="set-group glass">
    <div class="set-row"><label for="notif">Oturum hatırlatmaları</label><input class="toggle" id="notif" type="checkbox" ${st.notifications ? 'checked' : ''} data-setting="notifications"></div>
    <div class="set-row">
      <label for="before">Ne zaman</label>
      <select class="input" id="before" data-setting="notifyBefore">${[0, 5, 10, 15].map((v) => `<option value="${v}"${Number(st.notifyBefore) === v ? ' selected' : ''}>${v ? `${v} dk önce` : 'Tam zamanında'}</option>`).join('')}</select>
    </div>
  </div>

  <h2 class="set-title">Veri</h2>
  <div class="set-group glass">
    <div class="set-row"><span class="lbl">Yedeği kopyala</span><button class="btn glass" data-action="settings:exportData">${icon('copy', 16)} Kopyala</button></div>
    <div class="set-row"><span class="lbl">Yedekten geri yükle</span><button class="btn glass" data-action="settings:importSheet">${icon('download', 16)} Yükle</button></div>
  </div>

  <h2 class="set-title">Uygulama</h2>
  <div class="set-group glass">
    <div class="set-row"><span class="lbl">Sürüm</span><span class="muted">${BUILD ? `v${BUILD}` : 'Geliştirme'}</span></div>
    <div class="set-row"><span class="lbl">Güncellemeler</span><button class="btn glass" data-action="settings:checkUpdate">${icon('refresh', 16)} Denetle</button></div>
    <div class="set-row stack"><span class="lbl">Masa saati (ESP32)</span><p class="hint">Firebase bağlantısı ve cihaz eşleştirme bir sonraki sürümde gelecek.</p></div>
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
  importData(data) {
    store.importJSON(data.yedek || '');
    closeSheet();
    toast('Yedek geri yüklendi.');
  }
};
