"""TFT_eSPI için yumuşak (anti-aliased) yazı tipleri üretir: python3 make_fonts.py

TFT_eSPI'nin yerleşik yazı tiplerinde Türkçe harfler (ç ğ ı İ ö ş ü) yok. Bu betik
gereken harfleri .vlw biçiminde çizip assets/fonts/<ad>.vlw dosyalarına yazar. Yazılıma
gömülmezler: tools/make_assets.py onları SD karttaki varlık paketine koyar (ui/font.h okur).
DAXEN VANE tasarımı: bütün arayüz Inter (SIL Open Font License 1.1, gömülü kullanım serbest).
Logonun serif yazısı yazı tipi değil, görseldir (tools/make_logo.py).
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

INTER = Path("/usr/share/fonts/opentype/inter")
OUT = Path(__file__).resolve().parent.parent / "assets" / "fonts"

TEXT = (
    "".join(chr(c) for c in range(0x20, 0x7F))
    + "ÇçĞğİıÖöŞşÜüÂâÎîÛû°·–—…→←×÷−√π±≤≥²³½“”‘’«»₺€•"
)
# Okuyucu ve notlar için Batı Avrupa harfleri (şarkı ve dosya adları da)
LATIN = "".join(chr(c) for c in range(0xC0, 0x100) if chr(c) not in "×÷")
DIGITS = " 0123456789:-"
NUMS = " 0123456789:.,%°+-−×÷=()^√π/!eE"

FONTS = [
    # (C adı, dosya, piksel boyutu, karakterler)
    ("F_XS", "Inter-SemiBold.otf", 12, TEXT),
    ("F_SM", "Inter-Medium.otf", 15, TEXT + LATIN),
    ("F_MD", "Inter-SemiBold.otf", 18, TEXT + LATIN),
    ("F_LG", "Inter-Regular.otf", 26, NUMS),
    ("F_XL", "Inter-Light.otf", 64, DIGITS),
]
# Büyük rakamlarda üst/alt boşluk bırakılmaz: ara belleğe sığsın.
TIGHT = {"F_XL"}


def load(path, size):
    return ImageFont.truetype(str(INTER / path), size)


def build(path, size, chars, tight=False):
    font = load(path, size)
    ascent, descent = font.getmetrics()
    glyphs = []
    for ch in sorted(set(chars), key=ord):
        # Rakamlar eşit genişlikte (saat yazısı kaymasın); diğer harfler normal
        # (tnum boşluğu da rakam genişliğine çıkarıyor).
        feats = ["tnum"] if ch.isdigit() else []
        adv = round(font.getlength(ch, features=feats))
        left, top, right, bottom = font.getbbox(ch, anchor="ls", features=feats)
        w, h = max(0, right - left), max(0, bottom - top)
        if ch == " " or w == 0 or h == 0:
            glyphs.append((ord(ch), 0, 0, adv, 0, 0, b""))
            continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-left, -top), ch, font=font, fill=255, anchor="ls", features=feats)
        glyphs.append((ord(ch), h, w, adv, -top, left, img.tobytes()))

    if tight:
        ascent = max(g[4] for g in glyphs if g[1])
        descent = max(max(0, g[1] - g[4]) for g in glyphs if g[1])
    data = bytearray()
    data += struct.pack(">IIIIII", len(glyphs), 11, size, 0, ascent, descent)
    for code, h, w, adv, dy, dx, _ in glyphs:
        data += struct.pack(">IIIIiiI", code, h, w, adv, dy, dx, 0)
    for *_, bmp in glyphs:
        data += bmp
    return bytes(data)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, path, size, chars in FONTS:
        blob = build(path, size, chars, name in TIGHT)
        (OUT / f"{name}.vlw").write_bytes(blob)
        print(f"yazıldı: assets/fonts/{name}.vlw ({path} {size}px, {len(set(chars))} karakter, {len(blob)} bayt)")


if __name__ == "__main__":
    main()
