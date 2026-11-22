-- ad: Mayın Tarlası
-- simge: bayrak
-- surum: 1.0
-- izin: ekran, dosya
-- aciklama: Klasik mayın tarlası (12 × 8, 14 mayın), en iyi süre saklanır. Dokun: aç; basılı tut ya da Bayrak düğmesi: işaretle.
local W, H, U = ekran.en, ekran.boy, ekran.ust
local C, R, M = 12, 8, 14
local S = 22                       -- hücre boyu
local X0, Y0 = (W - C * S) // 2, U + 28
local mayin, acik, bayrak, sayi = {}, {}, {}, {}
local kuruldu, bitti, kazandi = false, false, false
local bayrakKip, acilan, bayraklar = false, 0, 0
local basla, sure, enIyi = 0, 0, nil
local basX, basY, basT = 0, 0, 0
local RENK = { renk.mavi, renk.yesil, renk.kirmizi, renk.mor, renk.turuncu, renk.sari, renk.beyaz, renk.gumus }

local function k(c, r) return r * C + c end

local function komsular(c, r, f)
  for dr = -1, 1 do
    for dc = -1, 1 do
      local nc, nr = c + dc, r + dr
      if (dc ~= 0 or dr ~= 0) and nc >= 0 and nc < C and nr >= 0 and nr < R then f(nc, nr) end
    end
  end
end

local function yeni()
  mayin, acik, bayrak, sayi = {}, {}, {}, {}
  kuruldu, bitti, kazandi, acilan, bayraklar, sure = false, false, false, 0, 0, 0
end

-- İlk açılan hücre ve komşuları güvenli: mayınlar ilk dokunuştan sonra konur
local function kur(c0, r0)
  local yasak = {}
  yasak[k(c0, r0)] = true
  komsular(c0, r0, function(c, r) yasak[k(c, r)] = true end)
  local n = 0
  while n < M do
    local i = math.random(0, C * R - 1)
    if not mayin[i] and not yasak[i] then mayin[i] = true; n = n + 1 end
  end
  for r = 0, R - 1 do
    for c = 0, C - 1 do
      local s = 0
      komsular(c, r, function(nc, nr) if mayin[k(nc, nr)] then s = s + 1 end end)
      sayi[k(c, r)] = s
    end
  end
  kuruldu, basla = true, zaman.ms()
end

local function hucre(c, r)
  local x, y, i = X0 + c * S, Y0 + r * S, k(c, r)
  if acik[i] then
    ekran.dikdortgen(x, y, S - 1, S - 1, mayin[i] and renk.kirmizi or renk.yuzey)
    if mayin[i] then
      ekran.daire(x + S // 2, y + S // 2, 5, renk.siyah, true)
    elseif sayi[i] > 0 then
      ekran.yazi(tostring(sayi[i]), x + S // 2, y + 3, RENK[sayi[i]], 2, "orta")
    end
  else
    ekran.dikdortgen(x, y, S - 1, S - 1, renk.cizgi)       -- kapalı hücre: kabarık
    ekran.dikdortgen(x, y, S - 1, 1, renk.soluk)
    ekran.dikdortgen(x, y, 1, S - 1, renk.soluk)
    if bayrak[i] then
      ekran.cizgi(x + 8, y + 4, x + 8, y + 17, renk.gumus, 2)
      ekran.dikdortgen(x + 9, y + 4, 7, 6, renk.kirmizi)
    elseif bitti and mayin[i] then
      ekran.daire(x + S // 2, y + S // 2, 5, renk.soluk, true)
    end
  end
end

local function ust()
  ekran.dikdortgen(0, U, W, 24, renk.zemin)
  local metin
  if bitti then
    metin = kazandi and ("Kazandın! " .. sure .. " sn") or "Mayına bastın"
  else
    metin = "Mayın " .. (M - bayraklar) .. " · " .. sure .. " sn"
    if not kuruldu and enIyi then metin = metin .. " · en iyi " .. enIyi end
  end
  ekran.yazi(metin, 8, U + 5, bitti and renk.vurgu or renk.gumus, 2)
  local bx = W - 92
  ekran.dikdortgen(bx, U + 2, 84, 20, bayrakKip and renk.yuzey2 or renk.yuzey)
  ekran.dikdortgen(bx, U + 2, 84, 20, bayrakKip and renk.vurgu or renk.cizgi, false)
  ekran.yazi(bitti and "Yeni oyun" or "Bayrak", bx + 42, U + 5, bayrakKip and renk.beyaz or renk.gumus, 1, "orta")
end

function baslat()
  local s = dosya.oku("rekor.txt")
  enIyi = s and tonumber(s) or nil
end

function ciz()
  ekran.temizle()
  ust()
  for r = 0, R - 1 do
    for c = 0, C - 1 do hucre(c, r) end
  end
end

local function hepsiniGoster()
  for r = 0, R - 1 do
    for c = 0, C - 1 do
      if mayin[k(c, r)] then hucre(c, r) end
    end
  end
end

local function ac(c0, r0)
  if not kuruldu then kur(c0, r0) end
  local i0 = k(c0, r0)
  if acik[i0] or bayrak[i0] then return end
  if mayin[i0] then
    acik[i0] = true
    bitti = true
    hucre(c0, r0)
    hepsiniGoster()
    ust()
    return
  end
  local yigin = { {c0, r0} }
  while #yigin > 0 do
    local p = table.remove(yigin)
    local c, r = p[1], p[2]
    local i = k(c, r)
    if not acik[i] and not bayrak[i] then
      acik[i] = true
      acilan = acilan + 1
      hucre(c, r)
      if sayi[i] == 0 then komsular(c, r, function(nc, nr) if not acik[k(nc, nr)] then yigin[#yigin + 1] = {nc, nr} end end) end
    end
  end
  if acilan == C * R - M then
    bitti, kazandi = true, true
    if not enIyi or sure < enIyi then
      enIyi = sure
      dosya.yaz("rekor.txt", tostring(sure))
    end
    ust()
  end
end

local function isaretle(c, r)
  local i = k(c, r)
  if acik[i] then return end
  bayrak[i] = not bayrak[i]
  bayraklar = bayraklar + (bayrak[i] and 1 or -1)
  hucre(c, r)
  ust()
end

local function hucreAt(x, y)
  local c, r = (x - X0) // S, (y - Y0) // S
  if c < 0 or c >= C or r < 0 or r >= R then return nil end
  return c, r
end

function bas(x, y)
  basX, basY, basT = x, y, zaman.ms()
end

function birak(x, y)
  if basY < U + 24 then  -- üst satır: düğme
    if basX >= W - 92 then
      if bitti then yeni(); ciz() else bayrakKip = not bayrakKip; ust() end
    end
    return
  end
  if bitti then return end
  local c, r = hucreAt(basX, basY)
  if not c then return end
  local uzun = zaman.ms() - basT >= 450
  if uzun or bayrakKip then isaretle(c, r) else ac(c, r) end
end

function adim(dt)
  if kuruldu and not bitti then
    local s = (zaman.ms() - basla) // 1000
    if s ~= sure then sure = s; ust() end
  end
end
