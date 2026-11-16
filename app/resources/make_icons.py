"""Uygulama ikonlarını üretir: python3 make_icons.py

Çıktı: android/res/ altına Android'in beklediği mipmap klasörleri.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).parent / "android" / "res"
NAVY = (10, 15, 36, 255)
S = 1024  # ana çizim boyutu


def blob(size, color, box, blur):
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse(box, fill=color)
    return layer.filter(ImageFilter.GaussianBlur(blur))


def artwork():
    img = Image.new("RGBA", (S, S), NAVY)
    img = Image.alpha_composite(img, blob(S, (109, 74, 255, 210), (-120, -140, 640, 620), 120))
    img = Image.alpha_composite(img, blob(S, (6, 182, 212, 170), (520, 380, 1180, 1040), 130))
    img = Image.alpha_composite(img, blob(S, (249, 115, 22, 120), (-80, 640, 520, 1200), 140))

    # Ortadaki cam kart
    glass = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(glass)
    box = (262, 262, 762, 762)
    g.rounded_rectangle(box, radius=120, fill=(255, 255, 255, 46), outline=(255, 255, 255, 120), width=6)
    g.line((340, 268, 684, 268), fill=(255, 255, 255, 150), width=4)
    img = Image.alpha_composite(img, glass)

    # Saat halkası ve onay işareti
    sym = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(sym)
    d.arc((352, 352, 672, 672), start=-60, end=250, fill=(255, 255, 255, 255), width=34)
    d.line([(432, 520), (494, 582), (626, 446)], fill=(125, 211, 252, 255), width=44, joint="curve")
    for x, y in [(432, 520), (626, 446)]:
        d.ellipse((x - 22, y - 22, x + 22, y + 22), fill=(125, 211, 252, 255))
    img = Image.alpha_composite(img, sym)
    return img


def mask(size, kind):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    if kind == "round":
        d.ellipse((0, 0, size - 1, size - 1), fill=255)
    else:
        d.rounded_rectangle((0, 0, size - 1, size - 1), radius=int(size * 0.22), fill=255)
    return m


def main():
    art = artwork()
    densities = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
    for name, f in densities.items():
        folder = OUT / f"mipmap-{name}"
        folder.mkdir(parents=True, exist_ok=True)
        fg = int(108 * f)
        art.resize((fg, fg), Image.LANCZOS).save(folder / "ic_launcher_foreground.png")
        legacy = int(48 * f)
        # Eski launcher'lar için: kenarlarda biraz boşluk bırakılmış çizim
        small = art.resize((legacy, legacy), Image.LANCZOS)
        for kind, fname in (("square", "ic_launcher.png"), ("round", "ic_launcher_round.png")):
            out = Image.new("RGBA", (legacy, legacy), (0, 0, 0, 0))
            out.paste(small, (0, 0), mask(legacy, kind))
            out.save(folder / fname)

    anydpi = OUT / "mipmap-anydpi-v26"
    anydpi.mkdir(parents=True, exist_ok=True)
    xml = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/calisma_icon_background"/>\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>\n'
        '</adaptive-icon>\n'
    )
    (anydpi / "ic_launcher.xml").write_text(xml, encoding="utf-8")
    (anydpi / "ic_launcher_round.xml").write_text(xml, encoding="utf-8")

    values = OUT / "values"
    values.mkdir(parents=True, exist_ok=True)
    (values / "calisma_colors.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        '    <color name="calisma_icon_background">#0A0F24</color>\n</resources>\n',
        encoding="utf-8",
    )
    art.resize((512, 512), Image.LANCZOS).save(Path(__file__).parent / "icon-preview.png")


if __name__ == "__main__":
    main()
