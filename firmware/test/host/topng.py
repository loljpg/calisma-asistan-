"""Önizleme görüntülerini PNG'ye çevirir.

  python3 topng.py <klasör>                     klasördeki *.ppm → *.png
  python3 topng.py <klasör> <pano.png> ad ...   ayrıca verilen ekranlardan yan yana pano
"""
import glob
import os
import sys

from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "out"
for f in sorted(glob.glob(os.path.join(out, "*.ppm"))):
    Image.open(f).save(f[:-4] + ".png")
    os.remove(f)

if len(sys.argv) > 3:
    names, cols, w, h = sys.argv[3:], 3, 320, 240
    ims = [Image.open(os.path.join(out, n + ".png")) for n in names]
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + 8) + 8, rows * (h + 8) + 8), (40, 40, 40))
    for i, im in enumerate(ims):
        sheet.paste(im, (8 + (i % cols) * (w + 8), 8 + (i // cols) * (h + 8)))
    sheet.save(sys.argv[2])
