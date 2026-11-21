-- ad: Pong
-- simge: oyun
-- surum: 1.0
-- izin: ekran
-- aciklama: Bilgisayara karşı pinpon. Raketi parmağınla sürükle; 7 sayıya ilk ulaşan kazanır.
local W, H, U = ekran.en, ekran.boy, ekran.ust
local TOP = U + 24          -- skorun altı
local PH, PW, R = 44, 6, 5
local PX, CX = 10, W - 16   -- raketlerin x'i
local py, cy, opy, ocy = 0, 0, -1, -1
local bx, by, obx, oby, vx, vy = 0, 0, -100, -100, 0, 0
local ps, cs, bitti, bekle = 0, 0, false, 0

local function sinir(v, a, b)
  if v < a then return a elseif v > b then return b end
  return v
end

local function servis(yon)
  bx, by = W / 2, (TOP + H) / 2
  local a = (math.random() - 0.5) * 1.2
  vx, vy = 3.2 * yon, 3.2 * math.sin(a)
  bekle = zaman.ms() + 700
end

local function skor()
  ekran.dikdortgen(0, U, W, 24, renk.zemin)
  ekran.yazi(tostring(ps), W / 2 - 20, U + 3, renk.gumus, 3, "sag")
  ekran.yazi(tostring(cs), W / 2 + 20, U + 3, renk.gumus, 3, "sol")
  ekran.dikdortgen(0, TOP - 1, W, 1, renk.cizgi)
end

local function orta()
  for y = TOP + 4, H - 6, 12 do ekran.dikdortgen(W / 2 - 1, y, 2, 6, renk.cizgi) end
end

local function raket(x, y, oy, c)
  if oy >= 0 then ekran.dikdortgen(x, math.floor(oy), PW, PH, renk.zemin) end
  ekran.dikdortgen(x, math.floor(y), PW, PH, c)
end

local function yeni()
  ps, cs, bitti = 0, 0, false
  py = (TOP + H) / 2 - PH / 2
  cy = py
  servis(math.random() < 0.5 and 1 or -1)
end

function baslat()
  yeni()
end

function ciz()
  ekran.temizle()
  skor()
  orta()
  opy, ocy, obx = -1, -1, -100
  raket(PX, py, -1, renk.beyaz)
  raket(CX, cy, -1, renk.gumus)
  if bitti then
    ekran.yazi(ps > cs and "Kazandın!" or "Bilgisayar kazandı", W / 2, 110, renk.vurgu, 3, "orta")
    ekran.yazi("Yeniden oynamak için dokun", W / 2, 140, renk.soluk, 1, "orta")
  end
end

local function sayi(kim)
  if kim == 1 then ps = ps + 1 else cs = cs + 1 end
  skor()
  if ps >= 7 or cs >= 7 then
    bitti = true
    ciz()
  else
    servis(kim == 1 and 1 or -1)
  end
end

function adim(dt)
  if bitti then return end
  -- bilgisayarın raketi topu biraz geriden izler
  local hedef = by - PH / 2
  if vx < 0 then hedef = (TOP + H) / 2 - PH / 2 end
  cy = cy + sinir(hedef - cy, -2.6, 2.6)
  cy = sinir(cy, TOP + 1, H - PH)
  if math.floor(cy) ~= math.floor(ocy) then raket(CX, cy, ocy, renk.gumus); ocy = cy end
  if math.floor(py) ~= math.floor(opy) then raket(PX, py, opy, renk.beyaz); opy = py end
  if zaman.ms() < bekle then return end
  bx, by = bx + vx, by + vy
  if by < TOP + R then by = TOP + R; vy = -vy end
  if by > H - R then by = H - R; vy = -vy end
  -- raketler: çarpılan yere göre açı, her vuruşta biraz hızlanır
  if vx < 0 and bx - R <= PX + PW and bx > PX and by >= py - R and by <= py + PH + R then
    bx = PX + PW + R
    local k = (by - (py + PH / 2)) / (PH / 2)
    vx = math.min(-vx * 1.06, 7)
    vy = k * 4
  elseif vx > 0 and bx + R >= CX and bx < CX + PW and by >= cy - R and by <= cy + PH + R then
    bx = CX - R
    local k = (by - (cy + PH / 2)) / (PH / 2)
    vx = -math.min(vx * 1.06, 7)
    vy = k * 4
  end
  if obx > -100 then ekran.daire(math.floor(obx), math.floor(oby), R, renk.zemin, true) end
  if math.abs(obx - W / 2) < R + 2 then orta() end
  if bx < -R then obx = -100; sayi(2); return end
  if bx > W + R then obx = -100; sayi(1); return end
  ekran.daire(math.floor(bx), math.floor(by), R, renk.vurgu, true)
  obx, oby = bx, by
end

function bas(x, y)
  if bitti then
    yeni()
    ciz()
    return
  end
  py = sinir(y - PH / 2, TOP + 1, H - PH)
end

function surukle(x, y)
  py = sinir(y - PH / 2, TOP + 1, H - PH)
end
