-- ad: Sayaç
-- simge: liste
-- surum: 1.1
-- izin: ekran, dosya
-- Kaldığı yeri hatırlayan sayaç. Alttaki düğmeler: -1 / +1, sayıya dokun: sıfırla.
local n = tonumber(ayar.al("n", "0")) or 0

local function dugme(x, y, w, h, yazi)
  ekran.dikdortgen(x, y, w, h, renk.yuzey, true)
  ekran.dikdortgen(x, y, w, h, renk.cizgi, false)
  ekran.yazi(yazi, x + w / 2, y + h / 2 - 11, renk.gumus, 3, "orta", renk.yuzey)
end

function ciz()
  ekran.temizle()
  ekran.yazi(tostring(n), 160, 56, renk.gumus, 5, "orta")
  ekran.yazi("Sıfırlamak için sayıya dokun", 160, 134, renk.soluk, 1, "orta")
  dugme(20, 168, 130, 52, "−1")
  dugme(170, 168, 130, 52, "+1")
end

function dokun(x, y)
  if y > 160 then
    if x < 160 then n = n - 1 else n = n + 1 end
  else
    n = 0
  end
  ayar.kaydet("n", n)
  ciz()
end
