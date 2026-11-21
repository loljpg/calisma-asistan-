-- ad: The Unknown
-- izin: ekran, wifi
-- Gizli özellik: gece saat 3 ile 4 arasında ana ekrandaki kediye dokun.
local b = sistem.bilgi()
local h, m = zaman.saat()
local w = wifi.durum()
local SATIR = {
  "...",
  string.format("saat %02d:%02d. herkes uyurken", h, m),
  string.format("bu kart %d saat %d dakikadır uyanık", b.calisma // 3600, b.calisma % 3600 // 60),
  string.format("belleğinde %d KB boşluk var", b.bos // 1024),
  w.bagli and string.format("dışarıda bir ağ fısıldıyor: %d dBm", w.sinyal) or "hiçbir ağ duymuyor seni",
  "bazı şeyler bilinmez kalır.",
}
local i, harf, son = 1, 0, 0

function ciz()
  ekran.temizle(renk.siyah)
  i, harf = 1, 0
end

function adim()
  if zaman.ms() - son < 70 then return end
  son = zaman.ms()
  if math.random(1, 9) == 1 then  -- bozulma
    local y = math.random(ekran.ust, 230)
    ekran.dikdortgen(0, y, 320, 2, ekran.renk(math.random(0, 60), 0, math.random(0, 40)))
    ekran.dikdortgen(0, y, 320, 2, renk.siyah)
  end
  if i > #SATIR then return end
  local s = SATIR[i]
  harf = harf + 1
  local y = ekran.ust + 20 + (i - 1) * 30
  local p = utf8.offset(s, harf + 1)
  ekran.yazi(p and s:sub(1, p - 1) or s, 16, y, i == #SATIR and renk.kirmizi or renk.gri, 2, "sol", renk.siyah)
  if not p then i, harf = i + 1, 0 end
end

function dokun() if i > #SATIR then sistem.cikis() end end
