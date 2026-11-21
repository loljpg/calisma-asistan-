-- ad: Zamanlayıcı
-- simge: kronometre
-- surum: 1.1
-- izin: ekran, bildirim, led
-- arkaplan: evet
-- Geri sayım. Uygulamadan çıksan da arka planda sayar, bitince haber verir.
local sure = 5 * 60
local kalan = sure
local calisiyor = false
local son = zaman.ms()

local function iki(n) return string.format("%02d", n) end

local function dugme(x, y, w, h, yazi, vurgu)
  local zemin = vurgu and renk.vurgu or renk.yuzey
  ekran.dikdortgen(x, y, w, h, zemin, true)
  ekran.dikdortgen(x, y, w, h, renk.cizgi, false)
  ekran.yazi(yazi, x + w / 2, y + h / 2 - 8, vurgu and renk.zemin or renk.gumus, 2, "orta", zemin)
end

local function sayi()
  ekran.dikdortgen(20, 44, 280, 76, renk.zemin, true)
  ekran.yazi(iki(kalan // 60) .. ":" .. iki(kalan % 60), 160, 50, calisiyor and renk.vurgu or renk.gumus, 5, "orta")
end

function ciz()
  ekran.temizle()
  sayi()
  ekran.yazi(calisiyor and "Çıksan da sayar" or "Süreyi ayarla, başlat", 160, 134, renk.soluk, 1, "orta")
  dugme(16, 168, 90, 48, "−1 dk")
  dugme(115, 168, 90, 48, calisiyor and "Durdur" or "Başlat", true)
  dugme(214, 168, 90, 48, "+1 dk")
end

function dokun(x, y)
  if y < 160 then return end
  if x < 110 then
    sure = math.max(60, sure - 60); kalan = sure; calisiyor = false
  elseif x < 210 then
    if kalan == 0 then kalan = sure end
    calisiyor = not calisiyor
    son = zaman.ms()
  else
    sure = math.min(99 * 60, sure + 60); kalan = sure; calisiyor = false
  end
  ciz()
end

function adim(dt)
  if not calisiyor then return end
  if zaman.ms() - son < 1000 then return end
  son = son + 1000
  kalan = kalan - 1
  if kalan <= 0 then
    kalan = 0
    calisiyor = false
    sistem.bildir("Zamanlayıcı: süre doldu")
    sistem.led(0, 0, 255)
    ciz()
  else
    sayi()
  end
end
