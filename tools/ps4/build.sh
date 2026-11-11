#!/usr/bin/env bash
# Builds Harissa64 for PS4 in WSL and makes its package.
#   bash tools/ps4/build.sh [upload]
# Needs the build set-up of the README ("Build from source"): clang 21,
# PacBrew OpenOrbis, libc++ 21 + runtime and RADV in ~/dolphinps4-build.
# Output: ~/h64ps4/out/IV0000-HRSA00064_00-HARISSA64PS40000.pkg
# ("upload": also sends it to the console's /data/pkg/ by FTP; PS4_HOST).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DPS4="$ROOT/third_party/dolphinps4"
source "$DPS4/scripts/ps4-env.sh"

BUILD="${H64_PS4_BUILD:-$HOME/h64ps4/build/harissa64}"
OUT="${H64_PS4_OUT:-$HOME/h64ps4/out}"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
# The package version is NN.NN: major and minor in the first number, the patch
# in the second (0.1.10-dev -> 01.10, 2.1.2 -> 21.02).
PKG_VERSION="$(echo "$VERSION" | awk -F'[.-]' '{printf "%02d.%02d", $1 * 10 + $2, $3}')"

if [ ! -f "$BUILD/build.ninja" ]; then
    ps4_cmake -S "$ROOT/platform/ps4" -B "$BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$DPS4/toolchain/ps4-love-modern.cmake" > "$BUILD.configure.log" 2>&1 \
        || { tail -30 "$BUILD.configure.log"; exit 1; }
fi
rc=0
ninja -C "$BUILD" > "$BUILD.ninja.log" 2>&1 || rc=$?
grep -E "error|warning: |undefined|FAILED|check-oelf" "$BUILD.ninja.log" | grep -v "Wunused-command-line" | head -60 || true
# A failed compile leaves the previous eboot in place: never package it.
[ "$rc" -eq 0 ] || { echo "build failed (ninja exit $rc, log $BUILD.ninja.log)" >&2; exit 1; }
eboot="$BUILD/harissa64_eboot/eboot.bin"
[ -f "$eboot" ] && [ ! "$BUILD/harissa64" -nt "$eboot" ] || { echo "build failed" >&2; exit 1; }

stage="$BUILD/stage"
rm -rf "$stage" && mkdir -p "$stage/sce_sys"
cp "$eboot" "$stage/"
# The cheat database (mupen64plus's mupencheat.txt): /app0/cheats/ in the app.
mkdir -p "$stage/cheats" && cp "$ROOT/third_party/mupen64plus/mupencheat.txt" "$stage/cheats/"
python3 -c "from PIL import Image; Image.open('$ROOT/assets/icon0.png').convert('RGB').resize((512,512)).save('$stage/sce_sys/icon0.png')"
mkdir -p "$OUT"
pkg="$(SFO_STYLE=plain bash "$DPS4/scripts/make-pkg.sh" "$stage" HRSA00064 "Harissa64" "$PKG_VERSION" HARISSA64PS40000 "$OUT" | tail -1)"
echo "package: $pkg (Harissa64 $VERSION, package version $PKG_VERSION)"

if [ "${1:-}" = upload ]; then
    : "${PS4_HOST:?set PS4_HOST to the address of the console}"
    curl -sS --max-time 600 -T "$pkg" "ftp://$PS4_HOST:2121/data/pkg/"
    echo "uploaded to $PS4_HOST:/data/pkg/"
fi
