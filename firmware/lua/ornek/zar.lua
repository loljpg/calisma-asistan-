-- ad: Zar
-- simge: zar
-- surum: 1.1
-- izin: ekran
-- İki zar atar. Dokun: at.
local d1, d2 = 1, 1
local kalan = 0

local noktalar = {
  {{0, 0}},
  {{-1, -1}, {1, 1}},
  {{-1, -1}, {0, 0}, {1, 1}},
  {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
  {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
  {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}},
}

local function zar(cx, cy, n)
  ekran.dikdortgen(cx - 40, cy - 40, 80, 80, renk.yuzey, true)
  ekran.dikdortgen(cx - 40, cy - 40, 80, 80, renk.cizgi, false)
  for _, p in ipairs(noktalar[n]) do
    ekran.daire(cx + p[1] * 22, cy + p[2] * 22, 7, renk.gumus, true)
  end
end

function ciz()
  ekran.temizle()
  zar(105, 110, d1)
  zar(215, 110, d2)
  ekran.yazi("Toplam " .. (d1 + d2), 160, 170, renk.gumus, 3, "orta")
  ekran.yazi("Atmak için dokun", 160, 204, renk.soluk, 1, "orta")
end

function dokun()
  kalan = 10
end

function adim(dt)
  if kalan > 0 then
    kalan = kalan - 1
    d1, d2 = math.random(6), math.random(6)
    ciz()
  end
end
