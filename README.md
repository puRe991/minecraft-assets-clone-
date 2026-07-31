# MiniCraft — a voxel sandbox for Windows (32-bit & 64-bit)

A small, **from-scratch** Minecraft-style voxel sandbox game for Windows.
Written in plain C using only the Win32 API + GDI — **no external libraries,
no engine, no copyrighted assets**. All block textures are generated
procedurally at runtime, and the world is rendered by a software voxel
raycaster (Amanatides & Woo DDA).

Ships as two standalone executables, one for each architecture:

| File | Target | Notes |
|------|--------|-------|
| `dist/MiniCraft-win64.exe` | 64-bit Windows (x86-64) | recommended |
| `dist/MiniCraft-win32.exe` | 32-bit Windows (i686)   | for older / 32-bit systems |

Both are fully self-contained (statically linked). They import only the
standard Windows system DLLs — `KERNEL32`, `USER32`, `GDI32`, `msvcrt` —
which are present on every Windows install from XP onward. **No installer,
no runtime, no DLLs to copy.** Just double-click the `.exe`.

## Features

- **Loads real Minecraft-format assets** — reads block textures from a
  standard resource-pack layout (`assets/minecraft/textures/block/*.png`) via
  a self-contained PNG decoder, and builds the world from them
- Procedurally generated terrain (hills, water, beaches, trees)
- First-person movement with gravity, jumping, collision, and a fly mode
- Break and place blocks; 11 block types with a hotbar
- **World save/load** — persist and resume your world (`world.sav`)
- Software raycasting renderer with per-face lighting and distance fog
- Multi-threaded rendering with adaptive resolution (see [Performance](#performance))
- Runs on old hardware — no GPU or OpenGL required

## Performance

Everything you see is raycast on the CPU, one ray per pixel, so the renderer
is the entire frame budget. Four things keep it fast:

- **Rays are clipped to the world box.** A ray aimed at the sky used to step
  its way to the 96-block draw distance finding nothing. Now it stops at the
  world boundary.
- **Empty space is skipped in bulk.** A coarse occupancy grid marks 8×8×8
  macro cells that contain no solid block, and the traversal jumps over runs
  of them a macro cell at a time instead of one block at a time.
- **Scanlines are split across worker threads**, one per logical core (rows
  are interleaved so no thread gets all the cheap ground rows). Threads are
  created once at startup and parked on an event between frames.
- **Adaptive resolution.** The game measures its own frame time and lowers the
  internal render resolution if it can't hold ~45 FPS, raising it again when
  there's headroom. The frame is upscaled to the window either way, so this
  costs sharpness rather than field of view. Press `T` to lock it.

Measured on a 4-core machine at the default 480×270 internal resolution:

| Camera | Before | After | |
|--------|--------|-------|-|
| Looking down at terrain | 72 FPS | 204 FPS | 2.8× |
| Horizon | 35 FPS | 172 FPS | 4.9× |
| Looking up | 21 FPS | 193 FPS | 9.3× |
| Straight up at open sky | 16 FPS | 222 FPS | 13.7× |
| **Average** | **26 FPS** | **196 FPS** | **7.5×** |

The gain is largest exactly where the old renderer was worst — the more sky in
frame, the more the old one stalled — so frame time is now roughly flat
regardless of where you look, instead of collapsing when you glance upward.

## Textures / assets

The game builds its blocks from PNG files in the Minecraft resource-pack layout:

```
assets/minecraft/textures/block/
    grass_block_top.png   grass_block_side.png   dirt.png
    stone.png             cobblestone.png        oak_log.png
    oak_log_top.png       oak_leaves.png         sand.png
    oak_planks.png        water_still.png        glass.png
```

A ready-made sample pack ships in `assets/` (and is copied into `dist/` next
to the executables, so a double-clicked `.exe` finds it). The title bar shows
how many textures were loaded from disk.

**Drop in your own pack:** replace the PNGs above with any 16×16 Minecraft
block textures and restart — the game uses them directly. Grayscale textures
that Minecraft tints by biome (`grass_block_top`, `oak_leaves`) are tinted at
load time. If a file is missing, the game falls back to a built-in
procedurally generated texture for that block, so it always runs.

## Controls

| Input | Action |
|-------|--------|
| Mouse | Look around |
| `W` `A` `S` `D` | Move |
| `Space` | Jump (fly up when flying) |
| `Left Shift` | Fly down (when flying) |
| `F` | Toggle fly mode |
| Left mouse | Break block |
| Right mouse | Place block |
| `1`–`9`, `0` | Select block to place (hotbar) |
| `K` / `L` | Save / load the world (`world.sav`) |
| `R` | Regenerate the world |
| `T` | Toggle adaptive resolution |
| `+` / `-` | Raise / lower render resolution (locks it) |
| `Esc` | Quit |

The title bar shows the current frame rate, internal render resolution and
render thread count.

A saved world (`world.sav`) is written next to the game and is loaded
automatically on the next launch.

## Building from source

The executables are cross-compiled on Linux with MinGW-w64:

```sh
sudo apt-get install gcc-mingw-w64-i686 gcc-mingw-w64-x86-64
./build.sh
```

Output lands in `dist/`.

### Tests

The platform-independent core (world generation, raycasting, physics,
texture generation) has a headless test harness that compiles and runs
natively on Linux:

```sh
gcc -O2 -std=c11 -Wall test/logic_test.c -o build/logic_test -lm
./build/logic_test
```

## Project layout

```
src/main.c            game + engine (single translation unit)
src/png.h             self-contained PNG codec (inflate + decode/encode)
tools/export_assets.c generates the sample resource pack
test/logic_test.c     headless tests for the platform-independent core
build.sh              cross-compile both binaries + export the sample pack
assets/               sample Minecraft-format resource pack
dist/                 the built .exe files (+ a copy of assets/)
```

## A note on scope

This is an **original** game inspired by the voxel-sandbox genre. It is **not**
Mojang's Minecraft and contains none of its code. The sample textures shipped
here are generated by this project itself (see `tools/export_assets.c`), not
copied from Minecraft. The engine reads the *Minecraft resource-pack format*
so you can supply your own textures, but a game engine cannot be reconstructed
from texture/sound assets alone — those are art, not code. MiniCraft is a
self-contained, legally clean program that gives you a real, playable voxel
world on Windows, built from asset files.
