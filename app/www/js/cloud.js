// Firebase bağlantısı (REST). SDK kullanmıyoruz; aynı uç noktaları ESP32 de kullanacak.

const IDENTITY = 'https://identitytoolkit.googleapis.com/v1/accounts:';
const TOKEN = 'https://securetoken.googleapis.com/v1/token';

const AUTH_ERRORS = {
  EMAIL_EXISTS: 'Bu e-postayla zaten bir hesap var. “Giriş yap”a dokun.',
  EMAIL_NOT_FOUND: 'Bu e-postayla bir hesap yok. Önce “Hesap oluştur”a dokun.',
  INVALID_PASSWORD: 'Şifre yanlış.',
  INVALID_LOGIN_CREDENTIALS: 'E-posta ya da şifre yanlış. Hesabın yoksa “Hesap oluştur”a dokun.',
  INVALID_EMAIL: 'E-posta adresi geçersiz.',
  WEAK_PASSWORD: 'Şifre en az 6 karakter olmalı.',
  MISSING_PASSWORD: 'Şifreyi yaz.',
  OPERATION_NOT_ALLOWED: 'Firebase’de E-posta/Şifre girişi kapalı. Authentication → Sign-in method bölümünden aç.',
  TOO_MANY_ATTEMPTS_TRY_LATER: 'Çok fazla deneme yapıldı. Biraz sonra tekrar dene.',
  USER_DISABLED: 'Bu hesap devre dışı bırakılmış.',
  TOKEN_EXPIRED: 'Oturumun süresi doldu. Tekrar giriş yap.',
  USER_NOT_FOUND: 'Hesap bulunamadı. Tekrar giriş yap.',
  INVALID_REFRESH_TOKEN: 'Oturum geçersiz. Tekrar giriş yap.',
  CONFIGURATION_NOT_FOUND: 'Firebase’de Authentication henüz başlatılmamış. Authentication → Başlayın’a dokun.'
};

function authError(data, status) {
  const raw = String(data?.error?.message || '');
  const code = raw.split(/[\s:]/)[0];
  if (AUTH_ERRORS[code]) return new Error(AUTH_ERRORS[code]);
  if (/API key not valid|API_KEY_INVALID/i.test(raw)) return new Error('Web API anahtarı geçersiz.');
  return new Error(`Giriş hatası (${status})${raw ? ': ' + raw : ''}`);
}

export function normalizeDbUrl(input) {
  let s = String(input || '').trim().replace(/\/+$/, '');
  if (s && !/^https?:\/\//i.test(s)) s = `https://${s}`;
  s = s.replace(/^http:\/\//i, 'https://');
  if (!/^https:\/\/[a-z0-9.-]+\.(firebasedatabase\.app|firebaseio\.com)$/i.test(s)) {
    throw new Error('Veritabanı adresi “.firebasedatabase.app” ile bitmeli.');
  }
  return s.toLowerCase();
}

async function request(url, options, offlineMsg = 'İnternete bağlanılamadı.') {
  let res;
  try {
    res = await fetch(url, options);
  } catch {
    throw new Error(offlineMsg);
  }
  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = null;
  }
  return { res, data };
}

// Giriş yapar ya da (create: true) yeni hesap açar.
export async function signIn({ apiKey, email, password, create = false }) {
  const url = `${IDENTITY}${create ? 'signUp' : 'signInWithPassword'}?key=${encodeURIComponent(apiKey)}`;
  const { res, data } = await request(url, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ email, password, returnSecureToken: true })
  });
  if (!res.ok) throw authError(data, res.status);
  return {
    uid: data.localId,
    email: data.email,
    idToken: data.idToken,
    refreshToken: data.refreshToken,
    expiresAt: Date.now() + (Number(data.expiresIn) || 3600) * 1000
  };
}

// Süresi dolan kimlik jetonunu yeniler.
export async function refreshToken({ apiKey, refreshToken: token }) {
  const { res, data } = await request(`${TOKEN}?key=${encodeURIComponent(apiKey)}`, {
    method: 'POST',
    headers: { 'content-type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ grant_type: 'refresh_token', refresh_token: token }).toString()
  });
  if (!res.ok) {
    const err = authError(data, res.status);
    if (res.status === 400 || res.status === 401 || res.status === 403) err.authExpired = true;
    throw err;
  }
  return {
    idToken: data.id_token,
    refreshToken: data.refresh_token,
    expiresAt: Date.now() + (Number(data.expires_in) || 3600) * 1000
  };
}

// Realtime Database REST istemcisi. getAuth(): Promise<{ dbUrl, idToken }>
export function makeDb(getAuth) {
  async function call(method, path, body) {
    const { dbUrl, idToken } = await getAuth();
    const url = `${dbUrl}/${path}.json?auth=${encodeURIComponent(idToken)}`;
    const opts = { method };
    if (body !== undefined) {
      opts.headers = { 'content-type': 'application/json' };
      opts.body = JSON.stringify(body);
    }
    const { res, data } = await request(url, opts, 'Buluta bağlanılamadı.');
    if (!res.ok) {
      if (res.status === 401) throw new Error('Bulut erişim izni yok. Realtime Database kurallarını yapıştırdığından emin ol.');
      if (res.status === 404) throw new Error('Veritabanı bulunamadı. Adresi kontrol et.');
      if (res.status === 423) throw new Error('Veritabanı devre dışı.');
      throw new Error(`Bulut hatası (${res.status})${data?.error ? ': ' + data.error : ''}`);
    }
    return data;
  }
  return {
    get: (p) => call('GET', p),
    put: (p, b) => call('PUT', p, b),
    patch: (p, b) => call('PATCH', p, b),
    del: (p) => call('DELETE', p)
  };
}
