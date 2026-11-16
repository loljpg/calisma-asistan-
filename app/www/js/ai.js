// Claude API bağlantısı.

export const MODELS = [
  { id: 'claude-haiku-5-5', name: 'Claude Haiku 5.5', note: 'Hızlı ve çok ucuz' },
  { id: 'claude-sonnet-5-5', name: 'Claude Sonnet 5.5', note: 'Daha akıllı, daha pahalı' }
];

export async function askClaude({ apiKey, model, system, user, maxTokens = 2000 }) {
  if (!apiKey) throw new Error('Önce Ayarlar’dan Claude API anahtarını gir.');
  let res;
  try {
    res = await fetch('https://api.anthropic.com/v1/messages', {
      method: 'POST',
      headers: {
        'content-type': 'application/json',
        'x-api-key': apiKey,
        'anthropic-version': '2023-06-01',
        'anthropic-dangerous-direct-browser-access': 'true'
      },
      body: JSON.stringify({
        model,
        max_tokens: maxTokens,
        system,
        messages: [{ role: 'user', content: user }]
      })
    });
  } catch {
    throw new Error('Claude’a bağlanılamadı. İnternet bağlantını kontrol et.');
  }

  const raw = await res.text();
  let data = null;
  try {
    data = JSON.parse(raw);
  } catch {
    data = null;
  }

  if (!res.ok) {
    const msg = data?.error?.message || '';
    if (res.status === 401) throw new Error('API anahtarı geçersiz. Ayarlar’dan kontrol et.');
    if (/credit balance/i.test(msg)) throw new Error('API kredin bitmiş. Claude Console’dan kredi yüklemen gerekiyor.');
    if (res.status === 429) throw new Error('Çok sık istek gönderildi. Biraz bekleyip tekrar dene.');
    if (res.status === 404 || (res.status === 400 && /model/i.test(msg))) {
      throw new Error('Seçili model bulunamadı. Ayarlar’dan başka bir model seç.');
    }
    if (res.status >= 500) throw new Error('Claude şu an yoğun. Birazdan tekrar dene.');
    throw new Error(`Claude hatası (${res.status})${msg ? ': ' + msg.slice(0, 160) : ''}`);
  }

  const text = (data?.content || []).filter((b) => b.type === 'text').map((b) => b.text).join('');
  if (data?.stop_reason === 'max_tokens') throw new Error('Cevap yarıda kesildi. Tekrar dene.');
  if (!text) throw new Error('Claude boş cevap döndü.');
  return text;
}
