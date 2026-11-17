"""TFT_eSPI için yumuşak (anti-aliased) yazı tipleri üretir: python3 make_fonts.py

TFT_eSPI'nin yerleşik yazı tiplerinde Türkçe harfler (ç ğ ı İ ö ş ü) yok. Bu betik
gereken harfleri .vlw biçiminde çizip src/fonts.h dosyasına yazar.
Arayüz: Inter. Başlıklar ve büyük rakamlar: Lora (SemiBold). İkisi de SIL Open Font
License 1.1 ile dağıtılır (gömülü kullanım serbest).
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

INTER = Path("/usr/share/fonts/opentype/inter")
LORA = Path("/usr/share/fonts/truetype/google-fonts/Lora-Variable.ttf")
OUT = Path(__file__).resolve().parent.parent / "src" / "fonts.h"

TEXT = (
    "".join(chr(c) for c in range(0x20, 0x7F))
    + "ÇçĞğİıÖöŞşÜüÂâÎîÛû°·–—…→"
)
DIGITS = " 0123456789:-"

FONTS = [
    # (C adı, dosya, piksel boyutu, karakterler); "lora" = Lora SemiBold (600)
    ("F_SMALL", "Inter-Medium.otf", 15, TEXT),
    ("F_TEXT", "Inter-SemiBold.otf", 18, TEXT),
    ("F_TITLE", "lora", 24, TEXT),
    ("F_BIG", "lora", 72, DIGITS),
]
# Büyük rakamlarda üst/alt boşluk bırakılmaz: 58 piksellik ara belleğe sığsın.
TIGHT = {"F_BIG"}


def load(path, size):
    if path == "lora":
        font = ImageFont.truetype(str(LORA), size)
        font.set_variation_by_axes([600])
        return font
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
    lines = [
        "// Bu dosya tools/make_fonts.py ile üretildi, elle değiştirme.",
        "// Yazı tipleri: Inter (rsms.me/inter) ve Lora (Cyreal), SIL Open Font License 1.1",
        "#pragma once",
        "#include <Arduino.h>",
        "",
    ]
    for name, path, size, chars in FONTS:
        blob = build(path, size, chars, name in TIGHT)
        lines.append(f"// {path} {size}px, {len(set(chars))} karakter, {len(blob)} bayt")
        lines.append(f"const uint8_t {name}[] PROGMEM = {{")
        for i in range(0, len(blob), 24):
            lines.append("  " + ",".join(f"0x{b:02X}" for b in blob[i:i + 24]) + ",")
        lines.append("};")
        lines.append("")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"yazıldı: {OUT} ({OUT.stat().st_size // 1024} KB kaynak)")


if __name__ == "__main__":
    main()
