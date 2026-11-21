-- ad: Nefes
-- simge: nabiz
-- surum: 1.0
-- izin: ekran
-- aciklama: Sınav öncesi sakinleşmek için yönlendirmeli nefes: kutu (4-4-4-4) ya da 4-7-8. Dokununca değişir.
local W, H, U = ekran.en, ekran.boy, ekran.ust
local CX, CY = W / 2, U + 80
local RMIN, RMAX = 24, 70
local KIP = {
  { ad = "Kutu nefesi", adim = { {"Nefes al", 4}, {"Tut", 4}, {"Ver", 4}, {"Tut", 4} } },
  { ad = "4-7-8", adim = { {"Nefes al", 4}, {"Tut", 7}, {"Ver", 8} } },
}
local kip, faz, t0, tur, oncekiR, oncekiS = 1, 1, 0, 0, 0, -1

local function yaricap(ms)
  local a = KIP[kip].adim[faz]
  local p = math.min(1, ms / (a[2] * 1000))
  if a[1] == "Nefes al" then return RMIN + (RMAX - RMIN) * p end
  if a[1] == "Ver" then return RMAX - (RMAX - RMIN) * p end
  -- tut: önceki aşamanın sonundaki boy
  local once = KIP[kip].adim[faz == 1 and #KIP[kip].adim or faz - 1][1]
  return once == "Nefes al" and RMAX or RMIN
end

local function yazilar(kalan)
  ekran.dikdortgen(0, CY + RMAX + 4, W, H - CY - RMAX - 4, renk.zemin)
  ekran.yazi(KIP[kip].adim[faz][1] .. "  " .. kalan, CX, CY + RMAX + 8, renk.beyaz, 3, "orta")
  ekran.yazi(KIP[kip].ad .. " · " .. tur .. ". tur", 8, H - 15, renk.soluk, 1, "sol")
  ekran.yazi("dokun: değiştir", W - 8, H - 15, renk.soluk, 1, "sag")
end

function baslat()
  t0 = zaman.ms()
end

function ciz()
  ekran.temizle()
  ekran.daire(CX, CY, RMAX + 2, renk.yuzey, false)
  oncekiR, oncekiS = 0, -1
end

function adim(dt)
  local ms = zaman.ms() - t0
  local a = KIP[kip].adim[faz]
  if ms >= a[2] * 1000 then
    faz = faz + 1
    if faz > #KIP[kip].adim then faz = 1; tur = tur + 1 end
    t0 = zaman.ms()
    ms = 0
    oncekiS = -1
  end
  local r = math.floor(yaricap(ms))
  if r ~= oncekiR then
    if r < oncekiR then ekran.daire(CX, CY, oncekiR, renk.zemin, true) end
    ekran.daire(CX, CY, RMAX + 2, renk.yuzey, false)
    ekran.daire(CX, CY, r, renk.vurgu, true)
    oncekiR = r
  end
  local kalan = KIP[kip].adim[faz][2] - ms // 1000
  if kalan ~= oncekiS then
    yazilar(kalan)
    oncekiS = kalan
  end
end

function dokun(x, y)
  kip = kip % #KIP + 1
  faz, tur, t0 = 1, 0, zaman.ms()
  ciz()
end
