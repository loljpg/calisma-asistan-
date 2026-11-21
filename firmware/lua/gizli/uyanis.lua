-- ad: Developer Awakening
-- izin: ekran
-- Gizli özellik: geliştirici seviyesi ilk kez açılınca; sonra Terminal'de "awaken".
local b = sistem.bilgi()
local SATIR = {
  "> geliştirici seviyesi doğrulandı",
  "> yazılım: fw-" .. b.surum,
  "> işlemci: " .. b.cip,
  string.format("> boş bellek: %d KB (en az %d KB)", b.bos // 1024, b.enaz // 1024),
  string.format("> uyanık: %d sa %d dk", b.calisma // 3600, b.calisma % 3600 // 60),
  "> Terminal artık sana daha çok şey söylüyor.",
  "> help, dev off, lua stop, logs clear …",
}
local KOL = 20
local damla = {}
for i = 1, KOL do damla[i] = math.random(-20, 0) end
local t0, yazilan, son = zaman.ms(), 0, 0

function ciz()
  ekran.temizle(renk.siyah)
  yazilan = 0
end

function adim()
  local t = zaman.ms() - t0
  if t < 2600 and zaman.ms() - son > 60 then  -- yağmur
    son = zaman.ms()
    for i = 1, KOL do
      local x, y = (i - 1) * 16 + 4, damla[i] * 14 + ekran.ust
      if y > ekran.ust then ekran.yazi(math.random(0, 1) == 1 and "1" or "0", x, y, renk.vurgu, 1, "sol", renk.siyah) end
      if y - 70 > ekran.ust then ekran.dikdortgen(x, y - 70, 10, 14, renk.siyah) end
      damla[i] = damla[i] + 1
      if y > 250 then damla[i] = math.random(-6, 0) end
    end
  elseif t >= 2600 then
    if yazilan == 0 then ekran.temizle(renk.siyah) end
    local n = math.min(#SATIR, (t - 2600) // 700 + 1)
    while yazilan < n do
      yazilan = yazilan + 1
      ekran.yazi(SATIR[yazilan], 12, ekran.ust + 8 + yazilan * 22, yazilan == 1 and renk.vurgu or renk.gumus, 1, "sol", renk.siyah)
    end
  end
end

function dokun() if yazilan == #SATIR then sistem.cikis() else t0 = zaman.ms() - 99999 end end
