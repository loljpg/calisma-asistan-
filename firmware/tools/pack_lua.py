#!/usr/bin/env python3
"""firmware/lua/ altındaki dosyaları (örnek uygulamalar, rehber) LZSS ile sıkıştırıp
src/apps/luapack.h'yi üretir. Kart dosyaları gerektiğinde SD karta açar (core/lzss.h).

  python3 tools/pack_lua.py           üret
  python3 tools/pack_lua.py --check   üretilmiş dosya kaynaklarla aynı mı (CI)

Biçim: her 8 öğe için bir bayrak baytı (bit 1: düz bayt, 0: eşleşme); eşleşme 2 bayt,
küçük uçlu: üst 12 bit uzaklık-1 (en fazla 4096), alt 4 bit uzunluk-3 (3..18).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "lua")
OUT = os.path.join(ROOT, "src", "apps", "luapack.h")

# (SD'deki hedef: grup, dosya adı) ← kaynak; grup 0: uygulamalar klasörü, 1: gizli klasörü
FILES = [
    (0, "merhaba.lua", "ornek/merhaba.lua"),
    (0, "zar.lua", "ornek/zar.lua"),
    (0, "sayac.lua", "ornek/sayac.lua"),
    (0, "top.lua", "ornek/top.lua"),
    (0, "zamanlayici.lua", "ornek/zamanlayici.lua"),
    (0, "REHBER.txt", "REHBER.txt"),
]


def lz(data, window=4096, maxlen=18):
    out = bytearray()
    heads = {}
    i, n = 0, len(data)

    def add(pos):
        if pos + 3 <= n:
            heads.setdefault(bytes(data[pos:pos + 3]), []).append(pos)

    while i < n:
        flagpos = len(out)
        out.append(0)
        flags = 0
        for b in range(8):
            if i >= n:
                break
            best, boff = 0, 0
            for p in reversed(heads.get(bytes(data[i:i + 3]), [])):
                if i - p > window:
                    break
                k = 0
                while k < maxlen and i + k < n and data[p + k] == data[i + k]:
                    k += 1
                if k > best:
                    best, boff = k, i - p
                    if k == maxlen:
                        break
            if best >= 3:
                v = ((boff - 1) << 4) | (best - 3)
                out += bytes([v & 255, v >> 8])
                for k in range(best):
                    add(i + k)
                i += best
            else:
                flags |= 1 << b
                out.append(data[i])
                add(i)
                i += 1
        out[flagpos] = flags
    return bytes(out)


def unlz(src):
    out = bytearray()
    i = 0
    while i < len(src):
        f = src[i]
        i += 1
        for _ in range(8):
            if i >= len(src):
                break
            if f & 1:
                out.append(src[i])
                i += 1
            else:
                v = src[i] | src[i + 1] << 8
                i += 2
                off, ln = (v >> 4) + 1, (v & 15) + 3
                for _ in range(ln):
                    out.append(out[-off])
            f >>= 1
    return bytes(out)


def render():
    lines = [
        "#pragma once",
        "// Otomatik üretildi: tools/pack_lua.py (kaynak: firmware/lua/). Elle değiştirme.",
        "// SD karta yazılan örnek uygulamalar ve rehber, LZSS ile sıkıştırılmış (core/lzss.h).",
        "",
        "struct LuaPackFile {",
        "  uint8_t group;  // 0: uygulamalar klasörü, 1: gizli klasörü",
        "  const char* name;",
        "  uint16_t len, plen;",
        "  const uint8_t* data;",
        "};",
        "",
    ]
    table = []
    raw = packed = 0
    for idx, (group, name, rel) in enumerate(FILES):
        data = open(os.path.join(SRC, rel), "rb").read()
        c = lz(data)
        assert unlz(c) == data, rel
        assert len(data) < 65536
        raw += len(data)
        packed += len(c)
        lines.append("static const uint8_t LP_%d[] = {  // %s, %d -> %d bayt" % (idx, rel, len(data), len(c)))
        for k in range(0, len(c), 24):
            lines.append("    " + ",".join("0x%02x" % b for b in c[k:k + 24]) + ",")
        lines.append("};")
        table.append('    {%d, "%s", %d, %d, LP_%d},' % (group, name, len(data), len(c), idx))
    lines.append("static const LuaPackFile LUA_PACK[] = {")
    lines += table
    lines.append("};")
    lines.append("// toplam %d bayt -> %d bayt" % (raw, packed))
    return "\n".join(lines) + "\n"


def main():
    text = render()
    if "--check" in sys.argv:
        cur = open(OUT).read() if os.path.exists(OUT) else ""
        if cur != text:
            print("src/apps/luapack.h güncel değil: python3 tools/pack_lua.py")
            sys.exit(1)
        print("luapack.h güncel")
        return
    open(OUT, "w").write(text)
    print(text.splitlines()[-1])


if __name__ == "__main__":
    main()
