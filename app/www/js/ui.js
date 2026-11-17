// Arayüz yardımcıları: ikonlar, kaçış, alt sayfa, bildirim balonu.

const PATHS = {
  settings: '<path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"></path><circle cx="12" cy="12" r="3"></circle>',
  sparkles: '<path d="M12 3l1.9 5.1L19 10l-5.1 1.9L12 17l-1.9-5.1L5 10l5.1-1.9z"></path><path d="M19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z"></path>',
  check: '<path d="M20 6 9 17l-5-5"></path>',
  calendar: '<rect x="3" y="4" width="18" height="18" rx="3"></rect><path d="M16 2v4M8 2v4M3 10h18"></path>',
  list: '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"></path>',
  clipboard: '<rect x="8" y="2" width="8" height="4" rx="1"></rect><path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"></path>',
  chart: '<path d="M3 3v18h18"></path><path d="M7 16v-4M12 16V8M17 16v-7"></path>',
  plus: '<path d="M12 5v14M5 12h14"></path>',
  flame: '<path d="M8.5 14.5A2.5 2.5 0 0 0 11 12c0-1.38-.5-2-1-3-1.07-2.14-.22-4.05 2-6 .5 2.5 2 4.9 4 6.5 2 1.6 3 3.5 3 5.5a7 7 0 1 1-14 0c0-1.15.43-2.29 1-3a2.5 2.5 0 0 0 2.5 2.5z"></path>',
  play: '<path d="M7 4v16l13-8z"></path>',
  pause: '<path d="M8 4v16M16 4v16"></path>',
  stop: '<rect x="6" y="6" width="12" height="12" rx="2"></rect>',
  timer: '<circle cx="12" cy="13" r="8"></circle><path d="M12 9v4l2 2M9 2h6"></path>',
  x: '<path d="M18 6 6 18M6 6l12 12"></path>',
  trash: '<path d="M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14"></path>',
  edit: '<path d="M12 20h9"></path><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"></path>',
  forward: '<path d="M5 12h14M13 6l6 6-6 6"></path>',
  back: '<path d="M15 18l-6-6 6-6"></path>',
  copy: '<rect x="9" y="9" width="12" height="12" rx="2"></rect><path d="M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1"></path>',
  download: '<path d="M12 3v12M7 10l5 5 5-5M5 21h14"></path>',
  refresh: '<path d="M21 12a9 9 0 1 1-3-6.7L21 8"></path><path d="M21 3v5h-5"></path>',
  alert: '<path d="M12 9v4M12 17h.01"></path><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"></path>',
  undo: '<path d="M9 14 4 9l5-5"></path><path d="M4 9h11a5 5 0 0 1 0 10h-3"></path>'
};

export function icon(name, size = 20, stroke = 1.8) {
  return `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="${stroke}" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${PATHS[name] || ''}</svg>`;
}

const ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
export function esc(v) {
  return String(v ?? '').replace(/[&<>"']/g, (c) => ESC[c]);
}

export function options(list, selected) {
  return list.map((v) => `<option value="${esc(v)}"${v === selected ? ' selected' : ''}>${esc(v)}</option>`).join('');
}

export function radio(name, value, label, checked, attrs = '') {
  return `<label class="radio-pill"><input type="radio" name="${name}" value="${esc(value)}"${checked ? ' checked' : ''} ${attrs}><span>${esc(label)}</span></label>`;
}

export function pageHead(title, sub, right = '') {
  return `<header class="page-head"><div><span class="sub">${esc(sub)}</span><h1>${esc(title)}</h1></div>${right}</header>`;
}

// ---------- Alt sayfa ----------
let current = null;

export function openSheet(html, label = 'Pencere') {
  closeSheet(true);
  const wrap = document.createElement('div');
  wrap.className = 'sheet-wrap';
  wrap.innerHTML = `<div class="sheet-backdrop" data-action="ui:closeSheet"></div><div class="sheet surface" role="dialog" aria-modal="true" aria-label="${esc(label)}"><div class="grabber"></div>${html}</div>`;
  document.body.appendChild(wrap);
  requestAnimationFrame(() => requestAnimationFrame(() => wrap.classList.add('open')));
  current = wrap;
  return wrap.querySelector('.sheet');
}

export function setSheet(html, label) {
  if (!current) return openSheet(html, label);
  const sheet = current.querySelector('.sheet');
  sheet.innerHTML = `<div class="grabber"></div>${html}`;
  sheet.scrollTop = 0;
  return sheet;
}

export function closeSheet(immediate = false) {
  if (!current) return false;
  const el = current;
  current = null;
  if (immediate) {
    el.remove();
    return true;
  }
  el.classList.remove('open');
  setTimeout(() => el.remove(), 280);
  return true;
}

export const sheetOpen = () => !!current;

export function sheetHead(title, sub = '') {
  return `<div class="sheet-head"><div><h2>${esc(title)}</h2>${sub ? `<span class="sub">${esc(sub)}</span>` : ''}</div><button class="icon-btn surface" data-action="ui:closeSheet" aria-label="Kapat">${icon('x', 20)}</button></div>`;
}

// ---------- Bildirim balonu ----------
let toastEl = null;
let toastTimer = null;

export function toast(msg, kind = 'info', ms = 2800) {
  if (!toastEl) {
    toastEl = document.createElement('div');
    toastEl.className = 'toast';
    toastEl.setAttribute('role', 'status');
    toastEl.setAttribute('aria-live', 'polite');
    document.body.appendChild(toastEl);
  }
  toastEl.textContent = msg;
  toastEl.classList.toggle('error', kind === 'error');
  toastEl.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toastEl.classList.remove('show'), kind === 'error' ? ms + 1500 : ms);
}

export async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    let ok = false;
    try {
      ok = document.execCommand('copy');
    } catch {
      ok = false;
    }
    ta.remove();
    return ok;
  }
}
