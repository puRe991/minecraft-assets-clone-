#!/usr/bin/env bash
# Cross-compile MiniCraft for Windows 32-bit and 64-bit using MinGW-w64.
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p dist

CFLAGS="-O2 -std=c11 -Wall -municode -DUNICODE -D_UNICODE"
# Note: WinMain uses ANSI strings, so drop -municode; keep plain ANSI build.
CFLAGS="-O2 -std=c11 -Wall"
LDFLAGS="-mwindows -lgdi32 -lm -s -static"

echo "==> Building 64-bit (x86_64) ..."
x86_64-w64-mingw32-gcc $CFLAGS src/main.c -o dist/MiniCraft-win64.exe $LDFLAGS

echo "==> Building 32-bit (i686) ..."
i686-w64-mingw32-gcc   $CFLAGS src/main.c -o dist/MiniCraft-win32.exe $LDFLAGS

echo "==> Exporting sample resource pack ..."
mkdir -p build
gcc -O2 -std=c11 -w tools/export_assets.c -o build/export_assets -lm
(cd . && ./build/export_assets >/dev/null)      # writes assets/ at repo root
# copy the pack next to the executables so a double-clicked .exe finds it
rm -rf dist/assets
mkdir -p dist/assets
cp -r assets/minecraft dist/assets/

echo "==> Done:"
ls -la dist/*.exe
file dist/*.exe
echo "sample pack: $(ls assets/minecraft/textures/block/*.png | wc -l) textures"
