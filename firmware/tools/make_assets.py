#!/usr/bin/env python3
"""Varlık paketi: yazı tipleri, logo ve Lua dosyaları tek dosyada (SD: /daxen/sys/varlik.bin).
Yazılıma gömülmezler; kart paketi her sürümün GitHub sayfasından kendisi indirir (sys/assets.h).
Yalnızca Python'un kendi kitaplığını kullanır (CI'da ek kurulum yok).

  python3 tools/make_assets.py --out <yol>   paketi yaz (ve src/core/varlik.h'yi güncelle)
  python3 tools/make_assets.py --check       src/core/varlik.h kaynaklarla aynı mı (CI)

Kaynaklar: assets/fonts/*.vlw (tools/make_fonts.py), assets/logo/*.bin (tools/make_logo.py),
lua/ (örnek uygulamalar, rehber, gizli özellikler).

Paket: "DXV1" | u32 sürüm (= CRC32, 8. bayttan sona) | u32 öğe sayısı |
       öğeler: ad[24] (NUL ile dolu) u32 başlangıç u32 uzunluk | veriler. Hepsi küçük uçlu.
Yazı tipi (DXF): u16 n, u16 satır yüksekliği, u16 en büyük yükselme, u16 yükselme (başlık),
       u16 boşluk genişliği, u16 0 | kod u16[n] | kayıt yeri u16[n] | en u8[n] | ilerleme u8[n] |
       sol kayma i8[n] | kayıtlar: boy u8, üst kayma i8, satırlar (her satır (en+1)/2 bayt, 4 bit,
       üst yarım bayt önce). TFT_eSPI'nin .vlw ölçüleri aynen korunur (yerleşim değişmez).
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HDR = os.path.join(ROOT, "src", "core", "varlik.h")

FONTS = ["F_XS", "F_SM", "F_MD", "F_LG", "F_XL"]
LUA = [
    ("lua/merhaba.lua", "ornek/merhaba.lua"),
    ("lua/zar.lua", "ornek/zar.lua"),
    ("lua/sayac.lua", "ornek/sayac.lua"),
    ("lua/top.lua", "ornek/top.lua"),
    ("lua/zamanlayici.lua", "ornek/zamanlayici.lua"),
    ("lua/REHBER.txt", "REHBER.txt"),
    ("gizli/vane.lua", "gizli/vane.lua"),
    ("gizli/uyanis.lua", "gizli/uyanis.lua"),
    ("gizli/hayalet.lua", "gizli/hayalet.lua"),
    ("gizli/radar.lua", "gizli/radar.lua"),
    ("gizli/bilinmeyen.lua", "gizli/bilinmeyen.lua"),
]


def vlw_to_dxf(v):
    n, _, _, _, ascent, descent = struct.unpack(">IIIIII", v[:24])
    glyphs = []
    p = 24
    for _ in range(n):
        code, h, w, adv, dy, dx, _ = struct.unpack(">IIIIiiI", v[p:p + 28])
        glyphs.append([code, h, w, adv, dy, dx])
        p += 28
    max_desc = descent
    for code, h, w, adv, dy, dx in glyphs:  # TFT_eSPI loadMetrics ile aynı kural
        if h - dy > max_desc and ((0x20 < code < 0xA0 and code != 0x7F) or code > 0xFF):
            max_desc = h - dy
    y_adv = ascent + max_desc
    space = (ascent + descent) * 2 // 7
    recs = bytearray()
    offs = []
    head = 12 + n * 7
    for code, h, w, adv, dy, dx in glyphs:
        bmp = v[p:p + w * h]
        p += w * h
        if (head + len(recs)) % 2:
            recs.append(0)
        offs.append(head + len(recs))
        assert head + len(recs) < 65536 and 0 <= w < 256 and 0 <= h < 256 and -128 <= dy < 128 and -128 <= dx < 128
        recs += struct.pack("<Bb", h, dy)
        for y in range(h):
            row = bmp[y * w:(y + 1) * w]
            q = [min(15, (a * 15 + 127) // 255) for a in row] + [0]
            for i in range(0, w, 2):
                recs.append(q[i] << 4 | q[i + 1])
    out = bytearray(struct.pack("<HHHHHH", n, y_adv, ascent, ascent, space, 0))
    out += b"".join(struct.pack("<H", g[0]) for g in glyphs)
    out += b"".join(struct.pack("<H", o) for o in offs)
    out += bytes(g[2] for g in glyphs)
    out += bytes(g[3] for g in glyphs)
    out += b"".join(struct.pack("<b", g[5]) for g in glyphs)
    assert len(out) == head
    return bytes(out + recs)


def entries():
    ents = []
    for f in FONTS:
        ents.append(("f/" + f[2:].lower(), vlw_to_dxf(open(os.path.join(ROOT, "assets", "fonts", f + ".vlw"), "rb").read())))
    ents.append(("logo/l", open(os.path.join(ROOT, "assets", "logo", "LOGO_L.bin"), "rb").read()))
    ents.append(("logo/s", open(os.path.join(ROOT, "assets", "logo", "LOGO_S.bin"), "rb").read()))
    for name, rel in LUA:
        ents.append((name, open(os.path.join(ROOT, "lua", rel), "rb").read()))
    # Mağaza (apps/store.h): lua/magaza/ içindeki bütün uygulamalar, ada göre sıralı
    mdir = os.path.join(ROOT, "lua", "magaza")
    for f in sorted(os.listdir(mdir)) if os.path.isdir(mdir) else []:
        if f.endswith(".lua"):
            name = "magaza/" + f
            if len(name.encode()) > 23:
                sys.exit("mağaza dosya adı çok uzun (en çok 16 bayt): " + f)
            ents.append((name, open(os.path.join(mdir, f), "rb").read()))
    if len(ents) > 64:
        sys.exit("pakette en çok 64 dosya olabilir")
    return ents


def build():
    ents = entries()
    table = bytearray()
    data = bytearray()
    start = 12 + 32 * len(ents)
    for name, blob in ents:
        assert len(name) < 24
        while (start + len(data)) % 4:
            data.append(0)
        table += name.encode().ljust(24, b"\0") + struct.pack("<II", start + len(data), len(blob))
        data += blob
    body = struct.pack("<I", len(ents)) + table + data
    ver = zlib.crc32(body) & 0xFFFFFFFF
    return b"DXV1" + struct.pack("<I", ver) + body, ver, ents


def header(pack, ver, ents):
    lines = [
        "// Bu dosya tools/make_assets.py ile üretildi, elle değiştirme.",
        "// SD karttaki varlık paketinin (/daxen/sys/varlik.bin) beklenen sürümü ve boyu. Paket her",
        "// sürümün GitHub sayfasında; eksik ya da eskiyse kart indirir (sys/assets.h).",
        "#pragma once",
        "#define ASSET_VER 0x%08Xu" % ver,
        "#define ASSET_LEN %du" % len(pack),
        "// içerik:",
    ]
    for name, blob in ents:
        lines.append("//   %-22s %6d bayt" % (name, len(blob)))
    return "\n".join(lines) + "\n"


def main():
    pack, ver, ents = build()
    text = header(pack, ver, ents)
    if "--check" in sys.argv:
        cur = open(HDR, encoding="utf-8").read() if os.path.exists(HDR) else ""
        if cur != text:
            sys.exit("src/core/varlik.h varlıklarla uyuşmuyor: python3 tools/make_assets.py --out /tmp/varlik.bin")
        print("varlik.h güncel (%d bayt, sürüm %08x)" % (len(pack), ver))
        return
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    open(HDR, "w", encoding="utf-8").write(text)
    if out:
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        open(out, "wb").write(pack)
    print("varlık paketi: %d bayt, sürüm %08x%s" % (len(pack), ver, (" → " + out) if out else ""))


if __name__ == "__main__":
    main()
