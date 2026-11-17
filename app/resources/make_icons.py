"""Uygulama ikonlarını üretir: python3 make_icons.py

Çıktı: android/res/ altına Android'in beklediği mipmap klasörleri.
"""
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).parent / "android" / "res"
INK = (22, 23, 26, 255)
RED = (207, 68, 40, 255)
S = 1024  # ana çizim boyutu


def artwork():
    """Sentez tasarımı: mürekkep zemin, silik kareli doku, beyaz onay kutusu ve kırmızı kalem tiki."""
    img = Image.new("RGBA", (S, S), INK)
    grid = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(grid)
    for i in range(0, S + 1, 64):
        g.line((i, 0, i, S), fill=(255, 255, 255, 16), width=3)
        g.line((0, i, S, i), fill=(255, 255, 255, 16), width=3)
    img = Image.alpha_composite(img, grid)

    sym = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(sym)
    d.rounded_rectangle((330, 360, 650, 680), radius=64, outline=(255, 255, 255, 255), width=40)
    d.line([(410, 500), (500, 600), (700, 330)], fill=RED, width=58, joint="curve")
    for x, y in [(410, 500), (700, 330)]:
        d.ellipse((x - 29, y - 29, x + 29, y + 29), fill=RED)
    return Image.alpha_composite(img, sym)


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
        '    <color name="calisma_icon_background">#16171A</color>\n</resources>\n',
        encoding="utf-8",
    )
    art.resize((512, 512), Image.LANCZOS).save(Path(__file__).parent / "icon-preview.png")


if __name__ == "__main__":
    main()
