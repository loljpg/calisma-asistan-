-- ad: Deneme
-- simge: grafik
-- surum: 1.0
-- izin: ekran, dosya, bildirim
-- aciklama: Deneme sonuçlarını gir (doğru, yanlış); netler hesaplanır, kaydedilir, gelişim grafikte görünür.
local W, H, U = ekran.en, ekran.boy, ekran.ust
local DOSYA = "denemeler.txt"
local TUR = {
  TYT = { {"Türkçe", 40}, {"Sosyal", 20}, {"Matematik", 40}, {"Fen", 20} },
  AYT = { {"Matematik", 40}, {"Fizik", 14}, {"Kimya", 13}, {"Biyoloji", 13} },
}
local tur = "TYT"
local d, y = {0, 0, 0, 0}, {0, 0, 0, 0}
local sec, secYan = nil, nil  -- seçili satır ve "d" ya da "y"
local yazilan = ""
local gorunum = "giris"       -- "giris" ya da "gecmis"
local ROW0, ROWH = U + 48, 28
local TUS = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "sil", "tamam"}

local function virgul(v) return (string.format("%.2f", v):gsub("%.", ",")) end
local function net(i) return d[i] - y[i] / 4 end
local function toplam()
  local t = 0
  for i = 1, 4 do t = t + net(i) end
  return t
end

local function dugme(x, yy, w, h, metin, secili)
  ekran.dikdortgen(x, yy, w, h, secili and renk.yuzey2 or renk.yuzey)
  ekran.dikdortgen(x, yy, w, h, secili and renk.vurgu or renk.cizgi, false)
  ekran.yazi(metin, x + w / 2, yy + h / 2 - 7, secili and renk.beyaz or renk.gumus, 1, "orta")
end

local function satir(i)
  local ry = ROW0 + (i - 1) * ROWH
  local ders = TUR[tur][i]
  ekran.dikdortgen(0, ry, W, ROWH, renk.zemin)
  ekran.yazi(ders[1], 8, ry + 5, renk.beyaz, 2)
  ekran.yazi("/" .. ders[2], 96, ry + 8, renk.soluk, 1)
  local function kutu(x, v, yan)
    local s = sec == i and secYan == yan
    ekran.dikdortgen(x, ry + 2, 44, 23, s and renk.yuzey2 or renk.yuzey)
    ekran.dikdortgen(x, ry + 2, 44, 23, s and renk.vurgu or renk.cizgi, false)
    ekran.yazi(s and (yazilan .. "_") or tostring(v), x + 22, ry + 5, renk.beyaz, 2, "orta")
  end
  kutu(132, d[i], "d")
  kutu(182, y[i], "y")
  ekran.yazi(virgul(net(i)), W - 8, ry + 5, renk.gumus, 2, "sag")
end

local function altSatir()
  ekran.dikdortgen(0, ROW0 + 4 * ROWH, W, H - ROW0 - 4 * ROWH, renk.zemin)
  if sec then
    for k, t in ipairs(TUS) do
      local c, r = (k - 1) % 6, (k - 1) // 6
      dugme(4 + c * 52, ROW0 + 4 * ROWH + 4 + r * 26, 48, 23, t, t == "tamam")
    end
  else
    local yy = ROW0 + 4 * ROWH + 8
    ekran.yazi("Toplam net", 8, yy + 4, renk.gumus, 2)
    ekran.yazi(virgul(toplam()), W - 8, yy, renk.vurgu, 3, "sag")
  end
end

local function ust()
  ekran.dikdortgen(0, U, W, 44, renk.zemin)
  dugme(8, U + 6, 48, 24, "TYT", tur == "TYT")
  dugme(60, U + 6, 48, 24, "AYT", tur == "AYT")
  dugme(gorunum == "giris" and 196 or 150, U + 6, 56, 24, gorunum == "giris" and "Kaydet" or "Sil", false)
  dugme(256, U + 6, 58, 24, gorunum == "giris" and "Geçmiş" or "Giriş", false)
end

-- Kayıtlar: tarih;tür;d,y;d,y;d,y;d,y
local function kayitlar()
  local s = dosya.oku(DOSYA) or ""
  local l = {}
  for line in s:gmatch("[^\n]+") do
    local parca = {}
    for p in line:gmatch("[^;]+") do parca[#parca + 1] = p end
    if #parca == 6 and parca[2] == tur then
      local t = 0
      for i = 3, 6 do
        local dd, yy = parca[i]:match("(%d+),(%d+)")
        if dd then t = t + tonumber(dd) - tonumber(yy) / 4 end
      end
      l[#l + 1] = { tarih = parca[1], net = t }
    end
  end
  return l
end

local function gecmisCiz()
  ekran.dikdortgen(0, U + 44, W, H - U - 44, renk.zemin)
  local l = kayitlar()
  if #l == 0 then
    ekran.yazi("Henüz " .. tur .. " kaydı yok", W / 2, 120, renk.soluk, 2, "orta")
    return
  end
  local enCok = 0
  for _, q in ipairs(TUR[tur]) do enCok = enCok + q[2] end
  local gx, gy, gw, gh = 34, U + 56, W - 46, 96
  ekran.dikdortgen(gx, gy + gh, gw, 1, renk.cizgi)
  ekran.yazi(tostring(enCok), gx - 4, gy - 6, renk.soluk, 1, "sag")
  ekran.yazi("0", gx - 4, gy + gh - 8, renk.soluk, 1, "sag")
  local bas = math.max(1, #l - 11)
  local n = #l - bas + 1
  local px, pyy
  for k = bas, #l do
    local i = k - bas
    local x = gx + (n == 1 and gw / 2 or i * gw / (n - 1))
    local yy = gy + gh - math.max(0, l[k].net) * gh / enCok
    if px then ekran.cizgi(px, pyy, x, yy, renk.vurgu, 2) end
    ekran.daire(x, yy, 3, k == #l and renk.beyaz or renk.vurgu, true)
    px, pyy = x, yy
  end
  local yy = gy + gh + 10
  for k = #l, math.max(1, #l - 2), -1 do
    ekran.yazi(l[k].tarih, 8, yy, renk.gumus, 1)
    ekran.yazi(virgul(l[k].net) .. " net", W - 8, yy, renk.beyaz, 1, "sag")
    yy = yy + 16
  end
end

function ciz()
  ekran.temizle()
  ust()
  if gorunum == "gecmis" then
    gecmisCiz()
    return
  end
  ekran.yazi("Ders", 8, U + 34, renk.soluk, 1)
  ekran.yazi("D", 154, U + 34, renk.soluk, 1, "orta")
  ekran.yazi("Y", 204, U + 34, renk.soluk, 1, "orta")
  ekran.yazi("Net", W - 8, U + 34, renk.soluk, 1, "sag")
  for i = 1, 4 do satir(i) end
  altSatir()
end

local function bitir()
  if sec and #yazilan > 0 then
    local v = tonumber(yazilan)
    local soru = TUR[tur][sec][2]
    if secYan == "d" then d[sec] = math.min(v, soru - y[sec]) else y[sec] = math.min(v, soru - d[sec]) end
  end
  local i = sec
  sec, secYan, yazilan = nil, nil, ""
  if i then satir(i) end
  altSatir()
end

local function kaydet()
  local yil, ay, gun = zaman.tarih()
  local s = string.format("%04d-%02d-%02d;%s", yil or 0, ay or 0, gun or 0, tur)
  for i = 1, 4 do s = s .. ";" .. d[i] .. "," .. y[i] end
  dosya.ekle(DOSYA, s .. "\n")
  sistem.bildir(tur .. " kaydedildi: " .. virgul(toplam()) .. " net")
  d, y = {0, 0, 0, 0}, {0, 0, 0, 0}
  ciz()
end

local function sonuSil()
  local s = dosya.oku(DOSYA) or ""
  local satirlar, son = {}, nil
  for line in s:gmatch("[^\n]+") do satirlar[#satirlar + 1] = line end
  for k = #satirlar, 1, -1 do
    if satirlar[k]:match("^[^;]+;" .. tur .. ";") then son = k break end
  end
  if not son then return end
  table.remove(satirlar, son)
  dosya.yaz(DOSYA, #satirlar > 0 and (table.concat(satirlar, "\n") .. "\n") or "")
  sistem.bildir("Son " .. tur .. " kaydı silindi")
  ciz()
end

function dokun(x, yy)
  if yy < U + 34 then
    if x < 56 then tur = "TYT"; sec = nil; ciz()
    elseif x < 110 then tur = "AYT"; sec = nil; ciz()
    elseif gorunum == "giris" and x >= 196 and x < 254 then bitir(); kaydet()
    elseif gorunum == "gecmis" and x >= 150 and x < 208 then sonuSil()
    elseif x >= 256 then gorunum = gorunum == "giris" and "gecmis" or "giris"; sec = nil; ciz() end
    return
  end
  if gorunum ~= "giris" then return end
  local alt = ROW0 + 4 * ROWH
  if sec and yy >= alt + 4 then
    local c = math.min(5, math.max(0, (x - 4) // 52))
    local r = (yy - alt - 4) // 26
    local t = TUS[r * 6 + c + 1]
    if t == "tamam" then bitir()
    elseif t == "sil" then yazilan = yazilan:sub(1, -2); satir(sec)
    elseif t and #yazilan < 2 then yazilan = yazilan .. t; satir(sec) end
    return
  end
  local i = (yy - ROW0) // ROWH + 1
  if i >= 1 and i <= 4 and x >= 128 and x < 230 then
    if sec then bitir() end
    sec, secYan, yazilan = i, x < 180 and "d" or "y", ""
    satir(i)
    altSatir()
  elseif sec then
    bitir()
  end
end
