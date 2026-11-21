-- ad: Merhaba
-- simge: kod
-- surum: 1.1
-- izin: ekran
-- İlk Lua uygulaman: ekrana dokundukça sayı artar.
local sayi = 0

function ciz()
  ekran.temizle()
  ekran.yazi("Merhaba DAXEN!", ekran.en / 2, 64, renk.gumus, 3, "orta")
  ekran.yazi("Ekrana dokun", ekran.en / 2, 92, renk.gri, 2, "orta")
  ekran.yazi(tostring(sayi), ekran.en / 2, 130, renk.vurgu, 5, "orta")
end

function dokun(x, y)
  sayi = sayi + 1
  ciz()
end
