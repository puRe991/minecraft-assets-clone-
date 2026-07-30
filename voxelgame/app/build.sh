#!/usr/bin/env bash
# Cross-compile the runnable VoxelGame .exe for Windows 32-bit and 64-bit.
# Self-contained: statically linked, only Win32 + GDI system DLLs.
set -euo pipefail
cd "$(dirname "$0")/.."          # repo: voxelgame/
mkdir -p dist

SRCS="app/main.cpp
      src/world/BlockRegistry.cpp src/world/World.cpp src/world/TerrainGenerator.cpp
      src/noise/PerlinNoise.cpp
      src/physics/PlayerController.cpp
      src/save/ChunkCodec.cpp src/save/WorldSaver.cpp"

CXXFLAGS="-O2 -std=c++17 -Iinclude -Iapp -Wall"
LDFLAGS="-mwindows -lgdi32 -static -static-libgcc -static-libstdc++ -s"

echo "==> Building 64-bit ..."
x86_64-w64-mingw32-g++ $CXXFLAGS $SRCS -o dist/VoxelGame-win64.exe $LDFLAGS
echo "==> Building 32-bit ..."
i686-w64-mingw32-g++   $CXXFLAGS $SRCS -o dist/VoxelGame-win32.exe $LDFLAGS

echo "==> Done:"
ls -la dist/*.exe
file dist/*.exe
