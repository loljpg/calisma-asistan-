-- ad: Top
-- simge: oyun
-- surum: 1.1
-- izin: ekran
-- Yerçekimli zıplayan top. Parmağınla fırlat.
local x, y, vx, vy = 160, 100, 3, 0
local r = 12
local sx, sy, st = 0, 0, 0

local function top(c)
  ekran.daire(math.floor(x), math.floor(y), r, c, true)
end

function ciz()
  ekran.temizle()
  ekran.yazi("Sürükleyip bırak: fırlat", 160, 40, renk.soluk, 1, "orta")
  top(renk.vurgu)
end

function adim(dt)
  top(renk.zemin)
  vy = vy + 0.4
  x, y = x + vx, y + vy
  if x < r then x = r; vx = -vx * 0.9 end
  if x > ekran.en - r then x = ekran.en - r; vx = -vx * 0.9 end
  if y > ekran.boy - r then y = ekran.boy - r; vy = -vy * 0.82; vx = vx * 0.98 end
  if y < ekran.ust + r + 24 then y = ekran.ust + r + 24; vy = -vy end
  top(renk.vurgu)
end

function bas(px, py)
  sx, sy, st = px, py, zaman.ms()
end

function birak(px, py)
  local t = math.max(30, zaman.ms() - st)
  vx = (px - sx) / t * 12
  vy = (py - sy) / t * 12
end
