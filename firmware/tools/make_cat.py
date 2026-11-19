"""Ana ekrandaki kedinin karelerini üretir: python3 make_cat.py > ../src/cat_frames.txt

Harfler: W beyaz, G gölge, K koyu gölge, E göz, P pembe, . boş. Dış çizgi (O) otomatik eklenir.
Çıktıyı main.cpp'deki CAT_FRAMES dizisine yapıştır.
"""
F = {
'walk1': [
".WW........W...W.",
"WWWW......WK..PW.",
"WWW.......WWWWWW.",
".WW.......WWWWEWW",
"..WW......WWWWWWP",
"..WWW....WWWWWWW.",
"...WWWWWWWWWWWW..",
"...GWWWWWWWWWWG..",
"...GGWWWWWWWWGG..",
"...WK.KW...WK.W..",
"...WK.KW...WK.W..",
],
'walk2': [
".WW........W...W.",
"WWWW......WK..PW.",
"WWW.......WWWWWW.",
".WW.......WWWWEWW",
"..WW......WWWWWWP",
"..WWW....WWWWWWW.",
"...WWWWWWWWWWWW..",
"...GWWWWWWWWWWG..",
"...GGWWWWWWWWGG..",
"....WKW.....WKW..",
"....WKW.....WKW..",
],
'run': [
".WW........W...W.",
"WWWW......WK..PW.",
"WWW.......WWWWWW.",
".WW......WWWWWEWW",
"..WW.....WWWWWWWP",
"..WWW...WWWWWWWW.",
"...WWWWWWWWWWWW..",
"...GWWWWWWWWWWG..",
"..GGGWWWWWWWGGWW.",
".WW.GGG....GG..WW",
"WW..............W",
],
'bat': [
".WW........W...W.",
"WWWW......WK..PW.",
"WWW.......WWWWWW.",
".WW.......WWWWEWW",
"..WW......WWWWWWP",
"..WWW....WWWWWWW.",
"...WWWWWWWWWWWWWW",
"...GWWWWWWWWWWG.W",
"...GGWWWWWWWWGG..",
"...WK.KW...WK....",
"...WK.KW...WK....",
],
'sit': [
"........W...W....",
"........WK..PW...",
"........WWWWWW...",
"........WWWWEWW..",
".WW.....WWWWWWP..",
"WWWW...WWWWWWW...",
"WWW...WWWWWWWW...",
"WW...WWWWWWWWW...",
".WW..GWWWWWWWW...",
"..WWGGWWWWWWGW...",
"...GGGGWWWWGGW...",
],
'sleep': [
".................",
".................",
".................",
".................",
"...........W...W.",
"......WWWWWWK.PW.",
"...WWWWWWWWWWWWW.",
"..WWWWWWWWWWKKWWW",
".WWGWWWWWWWWWWWWP",
".WWGGWWWWWWWWWWW.",
"..WWWGGGGGGGGGW..",
],
}


def outline(rows):
    w = len(rows[0]) + 2
    h = len(rows) + 2
    g = [["."] * w for _ in range(h)]
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            g[y + 1][x + 1] = c
    out = [row[:] for row in g]
    for y in range(h):
        for x in range(w):
            if g[y][x] == ".":
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    X, Y = x + dx, y + dy
                    if 0 <= X < w and 0 <= Y < h and g[Y][X] != ".":
                        out[y][x] = "O"
                        break
    return ["".join(r) for r in out]


if __name__ == "__main__":
    names = ["walk1", "walk2", "run", "bat", "sit", "sleep"]
    first = outline(F["walk1"])
    print("static const int CAT_W = %d, CAT_H = %d;" % (len(first[0]), len(first)))
    print("static const char* const CAT_FRAMES[][CAT_H] = {")
    for n in names:
        rows = outline(F[n])
        print("    {" + ",\n     ".join('"%s"' % r for r in rows) + "},  // " + n)
    print("};")
