-- ad: Dark Radar
-- izin: ekran, bluetooth
-- Gizli özellik: Ağ → Bluetooth sekmesinin adına uzun bas ya da Terminal'de "radar".
-- Yalnızca gerçek tarama: yakındaki cihazların reklamları. Merkeze yakınlık sinyal gücüdür,
-- uzaklık değil. Hiçbir cihaza bağlanılmaz.
local CX, CY, R = 108, 136, 94
local aci, cihaz, sonTara, son = 0, {}, -20000, 0
local function konum(d)
  local h = 0
  for i = 1, #d.adres do h = (h * 31 + d.adres:byte(i)) % 3600 end
  local s = math.max(-100, math.min(-30, d.sinyal))
  local r = 10 + (-30 - s) / 70 * (R - 14)
  local a = h / 10 * math.pi / 180
  return CX + math.floor(r * math.cos(a)), CY + math.floor(r * math.sin(a)), a
end

local function zemin()
  ekran.daire(CX, CY, R, renk.siyah, true)
  for k = 1, 3 do ekran.daire(CX, CY, R * k // 3, ekran.renk(20, 60, 40), false) end
  ekran.cizgi(CX - R, CY, CX + R, CY, ekran.renk(20, 60, 40))
  ekran.cizgi(CX, CY - R, CX, CY + R, ekran.renk(20, 60, 40))
end

local function liste()
  ekran.dikdortgen(206, ekran.ust + 4, 114, 206, renk.siyah)
  ekran.yazi(#cihaz .. " cihaz", 210, ekran.ust + 8, renk.vurgu, 1, "sol", renk.siyah)
  for i = 1, math.min(8, #cihaz) do
    local d = cihaz[i]
    local ad = d.ad ~= "" and d.ad or (d.uretici ~= "" and d.uretici or d.adres:sub(10))
    if utf8.len(ad) and utf8.len(ad) > 10 then ad = ad:sub(1, (utf8.offset(ad, 11) or 11) - 1) end
    ekran.yazi(string.format("%d %s", d.sinyal, ad), 210, ekran.ust + 14 + i * 18, renk.gri, 1, "sol", renk.siyah)
  end
  ekran.yazi("merkeze yakın:", 210, 212, renk.soluk, 1, "sol", renk.siyah)
  ekran.yazi("güçlü sinyal", 210, 226, renk.soluk, 1, "sol", renk.siyah)
end

function ciz()
  ekran.temizle(renk.siyah)
  zemin()
  liste()
end

function adim()
  local now = zaman.ms()
  if not bluetooth.suruyor() and now - sonTara > 12000 then
    sonTara = now
    local ok, e = bluetooth.tara(10)
    if not ok then ekran.yazi(e or "tarama yok", 12, 226, renk.kirmizi, 1, "sol", renk.siyah) end
  end
  if now - son < 50 then return end
  son = now
  if math.floor(aci / (2 * math.pi)) ~= math.floor((aci + 0.12) / (2 * math.pi)) then  -- tur bitti
    cihaz = bluetooth.cihazlar()
    zemin()
    liste()
  end
  aci = aci + 0.12
  local x2, y2 = CX + math.floor(R * math.cos(aci)), CY + math.floor(R * math.sin(aci))
  ekran.cizgi(CX, CY, x2, y2, ekran.renk(40, 200, 120))
  local a0 = aci - 0.12
  ekran.cizgi(CX, CY, CX + math.floor(R * math.cos(a0)), CY + math.floor(R * math.sin(a0)), ekran.renk(10, 40, 26))
  for _, d in ipairs(cihaz) do
    local x, y, a = konum(d)
    local fark = (aci - a) % (2 * math.pi)
    if fark < 0.15 then ekran.daire(x, y, 3, ekran.renk(120, 255, 170), true) end
  end
end
