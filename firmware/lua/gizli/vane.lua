-- ad: Project VANE
-- izin: ekran
-- Gizli özellik: Ayarlar → Sistem'de logoya beş kez dokun.
local SATIR = {
  "Bir YKS çalışma asistanı olarak başladı.",
  "Sonra saat, hava, notlar ve oyunlar geldi.",
  "Sonra Lua, Terminal, ağ ve Bluetooth.",
  "Şimdi masada duran küçük bir bilgisayar.",
  "",
  "Fikir ve tasarım: Sinan",
  "Kod: Claude",
}
local b = sistem.bilgi()
local t0, yazilan, sonParilti = zaman.ms(), 0, 0
local yildiz = {}
for i = 1, 36 do yildiz[i] = { math.random(0, 319), math.random(ekran.ust + 1, 239), math.random(1, 3) } end

function ciz()
  ekran.temizle(renk.siyah)
  yazilan, sonParilti = 0, 0
end

function adim()
  local t = (zaman.ms() - t0) / 1000
  for _, s in ipairs(yildiz) do
    local y = s[2]
    local bos = not (y >= 34 and y <= 58 or y >= 82 and y <= 200 or y >= 218)  -- yazıların üstüne çizilmez
    if bos then ekran.piksel(s[1], y, renk.siyah) end
    s[1] = s[1] - s[3]
    if s[1] < 0 then s[1], s[2] = 319, math.random(ekran.ust + 1, 239) end
    if bos and s[2] == y then ekran.piksel(s[1], y, s[3] == 3 and renk.gumus or renk.soluk) end
  end
  if zaman.ms() - sonParilti > 90 then
    sonParilti = zaman.ms()
    local g = math.floor(130 + 110 * math.abs(math.sin(t * 1.4)))
    ekran.yazi("PROJECT VANE", 160, 44, ekran.renk(g, g, math.min(255, g + 25)), 3, "orta", renk.siyah)
  end
  local n = math.min(#SATIR, math.floor(t / 1.1))
  while yazilan < n do
    yazilan = yazilan + 1
    ekran.yazi(SATIR[yazilan], 160, 70 + yazilan * 17, yazilan > 5 and renk.gumus or renk.gri, 1, "orta", renk.siyah)
    if yazilan == #SATIR then
      ekran.yazi("fw-" .. b.surum .. " · " .. b.cip, 160, 226, renk.soluk, 1, "orta", renk.siyah)
    end
  end
end

function dokun() t0 = zaman.ms() - 60000 end
