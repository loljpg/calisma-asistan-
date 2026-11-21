-- ad: Ghost Protocol
-- izin: ekran, led
-- Gizli özellik: Terminal'de "ghost". Ekran kararır, ışık söner; yalnızca soluk bir saat dolaşır.
-- Dokununca çıkar. Arka planda hiçbir şey çalıştırmaz.
local x, y, vx, vy = 90, 110, 0.35, 0.22
local son, iz = 0, ""
local function saat()
  local h, m = zaman.saat()
  return string.format("%02d:%02d", h, m)
end

function ciz()
  ekran.temizle(renk.siyah)
  sistem.led(0, 0, 0)
  ekran.yazi("HAYALET PROTOKOLÜ", 160, ekran.ust + 6, ekran.renk(40, 44, 52), 1, "orta", renk.siyah)
end

function adim()
  if zaman.ms() - son < 120 then return end
  son = zaman.ms()
  local w = ekran.genislik("88:88", 4)
  ekran.dikdortgen(math.floor(x) - 2, math.floor(y) - 2, w + 4, 44, renk.siyah)
  x, y = x + vx * 4, y + vy * 4
  if x < 4 or x + w > 316 then vx = -vx end
  if y < ekran.ust + 24 or y + 40 > 236 then vy = -vy end
  local g = 46 + math.floor(18 * math.sin(zaman.ms() / 900))
  ekran.yazi(saat(), math.floor(x), math.floor(y), ekran.renk(g, g + 4, g + 12), 4, "sol", renk.siyah)
end

function dokun() sistem.cikis() end
