"""DAXEN VANE logosunu ekran için 4 bitlik gri görüntüye çevirir: python3 make_logo.py

Kaynak: tools/daxen-logo.png (siyah zemin üstünde gümüş serif yazı). Yazının biçimi, DAXEN ile
VANE arasındaki boyut farkı, hizası ve gümüş tonları olduğu gibi korunur; yalnızca küçültülür.
Her piksel 0-15 arası parlaklıktır (ekranda siyah ile kaynağın en parlak gümüşü arasında).
"""
from pathlib import Path

import numpy as np
from PIL import Image

SRC = Path(__file__).resolve().parent / "daxen-logo.png"
OUT = Path(__file__).resolve().parent.parent / "src" / "ui" / "logo.h"
SIZES = [("LOGO_L", 236), ("LOGO_S", 112)]


def main():
    im = Image.open(SRC).convert("L")
    a = np.array(im)
    ys, xs = np.where(a > 24)
    box = (xs.min() - 2, ys.min() - 2, xs.max() + 3, ys.max() + 3)
    crop = im.crop(box)
    peak = int(np.array(crop).max())
    lines = [
        "// Bu dosya tools/make_logo.py ile üretildi, elle değiştirme.",
        "// DAXEN VANE logosu: satır satır, piksel başına 4 bit (üst yarım bayt önce), 0 siyah, 15 en parlak gümüş.",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        f"#define LOGO_PEAK {peak}  // kaynaktaki en parlak gümüş (0-255)",
    ]
    for name, w in SIZES:
        h = round(crop.height * w / crop.width)
        small = np.array(crop.resize((w, h), Image.LANCZOS)).astype(float)
        q = np.clip(np.round(small / peak * 15), 0, 15).astype(int)
        if w % 2:
            q = np.hstack([q, np.zeros((h, 1), int)])
        packed = bytearray()
        for row in q:
            for i in range(0, len(row), 2):
                packed.append((row[i] << 4) | row[i + 1])
        lines.append(f"#define {name}_W {w}")
        lines.append(f"#define {name}_H {h}")
        lines.append(f"const uint8_t {name}[] PROGMEM = {{")
        for i in range(0, len(packed), 24):
            lines.append("  " + ",".join(f"0x{b:02X}" for b in packed[i:i + 24]) + ",")
        lines.append("};")
        lines.append("")
        prev = Image.fromarray((q * peak / 15).astype(np.uint8))
        prev.save(f"/tmp/{name}.png")
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print("yazıldı:", OUT)


if __name__ == "__main__":
    main()
