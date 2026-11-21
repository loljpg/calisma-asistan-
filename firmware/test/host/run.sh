#!/usr/bin/env bash
# DAXEN VANE · bilgisayarda test ve önizleme (kart gerekmez; gerçek yazılım kodu sahte ekranla derlenir)
#
#   ./run.sh test      birim testleri + bütün ekran senaryoları + müzik yazılımı,
#                      AddressSanitizer ve UndefinedBehaviorSanitizer açık (bellek ve tanımsız davranış hataları)
#   ./run.sh preview   ekran görüntüleri: out/*.png (Python 3 ve Pillow gerekir)
#   ./run.sh preview s5   yalnızca bir senaryo
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/../../src" && pwd)"
LUA="$(cd "$HERE/../../lib/lua/src" && pwd)"
MODE="${1:-test}"
ONLY="${2:-}"
CXX="${CXX:-g++}"
CC="${CC:-gcc}"

case "$MODE" in
  test) OPT="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined" ;;
  preview) OPT="-O1 -g" ;;
  *) echo "kullanım: $0 test|preview [senaryo]"; exit 2 ;;
esac
BUILD="$HERE/build/$MODE"
OUT="$HERE/out"
mkdir -p "$BUILD/lua" "$OUT"
DEFS="-DHOST=1 -DFW_BUILD=999 -DFW_VARIANT_ST7789=1"
CXXF="-std=gnu++17 $OPT $DEFS -Wall -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-unused-but-set-variable"

stamp() { [ "$1" -nt "$2" ]; }  # $1 hedef $2 kaynaktan yeni mi

# --- ortak nesneler ---
stamp "$BUILD/WString.o" "$HERE/shim/WString.cpp" || $CXX $CXXF -w -I"$HERE/shim" -c "$HERE/shim/WString.cpp" -o "$BUILD/WString.o"
stamp "$BUILD/noniso.o" "$HERE/shim/stdlib_noniso.c" || $CC $OPT -w -I"$HERE/shim" -c "$HERE/shim/stdlib_noniso.c" -o "$BUILD/noniso.o"
stamp "$BUILD/runtime.o" "$HERE/runtime.cpp" || $CXX $CXXF -w -I"$HERE/shim" -c "$HERE/runtime.cpp" -o "$BUILD/runtime.o"
LUAOBJ=""
for c in "$LUA"/*.c; do
  o="$BUILD/lua/$(basename "$c" .c).o"
  stamp "$o" "$c" && stamp "$o" "$LUA/luaconf.h" || $CC $OPT -w -c "$c" -o "$o"
  LUAOBJ="$LUAOBJ $o"
done
COMMON="$BUILD/WString.o $BUILD/noniso.o $BUILD/runtime.o"
INC="-I$HERE -I$HERE/shim -I$SRC -I$SRC/core -I$LUA"

echo "derleniyor ($MODE)…"
$CXX $CXXF $INC "$HERE/preview.cpp" $COMMON $LUAOBJ -o "$BUILD/preview"
[ "$MODE" = test ] && $CXX $CXXF $INC "$HERE/tests.cpp" $COMMON $LUAOBJ -o "$BUILD/tests"

# --- geçici SD kart: örnek klasör ve dosyalar ---
SD="$(mktemp -d "${TMPDIR:-/tmp}/daxen-sd.XXXXXX")"
cleanup() { case "$SD" in */daxen-sd.*) rm -rf -- "$SD" ;; esac; }
trap cleanup EXIT
mkdir -p "$SD/Muzik/Sezen Aksu" "$SD/calisma" "$SD/daxen/notlar" "$SD/daxen/kartlar" "$SD/daxen/cizim" "$SD/daxen/kitaplar"
for t in "Barış Manço - Gülpembe" "MFÖ - Ele Güne Karşı" "Tarkan - Kuzu Kuzu" "Sezen Aksu - Gidiyorum"; do : >"$SD/Muzik/$t.mp3"; done
for t in Firuze Gidiyorum "Hadi Bakalım" "Yalnızlık Senfonisi"; do : >"$SD/Muzik/Sezen Aksu/$t.mp3"; done
printf 'süt, ekmek\n' >"$SD/daxen/notlar/alisveris.txt"
mkdir -p "$SD/daxen/sys" && truncate -s 1382048 "$SD/daxen/sys/muzik-cyd_st7789.bin"  # müzikten kalan eski modül
python3 "$HERE/../../tools/make_assets.py" --out "$SD/daxen/sys/varlik.bin" >/dev/null  # yazı tipleri, logo, Lua dosyaları
printf 'Türev | Değişim oranı\nİntegral | Alan\n' >"$SD/daxen/kartlar/ornek.txt"
: >"$SD/foto.jpg"
export DAXEN_SD="$SD" DAXEN_OUT="$OUT"
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1" UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

if [ "$MODE" = test ]; then
  echo "birim testleri…"
  "$BUILD/tests"
  echo "ekran senaryoları…"
  "$BUILD/preview" >/dev/null
  rm -f "$OUT"/*.ppm
  echo "hepsi geçti"
else
  "$BUILD/preview" $ONLY >/dev/null
  python3 "$HERE/topng.py" "$OUT"
  echo "görüntüler: $OUT"
fi
