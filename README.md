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

- **Survival & Creative modes** — Survival has health, fall damage, and
  block gathering (breaking a block drops it into your inventory, placing
  consumes it); Creative gives flight and unlimited blocks. Pick the mode on
  the create-world screen, or switch in game with `G` / `/gamemode`.
- **Minecraft-style inventory** (`E`) — a 9×4 slot grid (hotbar + 3 storage
  rows); click to pick up / drop / swap stacks, right-click to split, scroll
  or `1`–`9` to pick the hotbar slot. Survival shows a heart health bar.
- **Main menu** with a rotating world panorama, keyboard + mouse navigation
- **Create-world screen with seeds** — type a seed (number or text) like in
  Minecraft; blank = random; choose Survival/Creative with Left/Right
- **In-game console** (`T` or `/`) with commands: `tp`, `give`, `setblock`,
  `fill`, `time`, `gamemode`, `fly`, `speed`, `fov`, `regen`, `seed`, `help`, …
- **Settings screen** — FOV, render distance, mouse sensitivity, move speed,
  and daylight, all adjustable live
- **Day / night** lighting
- **Chunk-streamed world** — 16×16 chunks generate and unload around the
  player as you walk, so the world extends effectively without limit in X/Z
  (including negative coordinates)
- **Loads real Minecraft-format assets** — reads block textures from a
  standard resource-pack layout (`assets/minecraft/textures/block/*.png`) via
  a self-contained PNG decoder, and builds the world from them
- Procedurally generated, deterministic terrain (hills, water, beaches, trees)
- First-person movement with gravity, jumping, collision, and a fly mode
- Break and place blocks; 11 block types with a hotbar
- **World save/load** — persist and resume your world (`world.sav`)
- Software raycasting renderer with per-face lighting and distance fog
- Runs on old hardware — no GPU or OpenGL required

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
| `Left Shift` | Fly down (creative fly) |
| `E` | Open / close the inventory |
| `F` | Toggle fly (creative only) |
| `G` | Toggle survival / creative |
| Left mouse | Break block (collects it in survival) |
| Right mouse | Place block (consumes it in survival) |
| `1`–`9` / scroll | Select hotbar slot |
| `T` or `/` | Open the console |
| `K` / `L` | Save / load the world (`world.sav`) |
| `R` | Regenerate the world |
| `Esc` | Pause menu / close overlay / back |
| Arrows + `Enter` | Navigate menus (mouse click also works) |

In the inventory: **left-click** picks up / drops / swaps a whole stack,
**right-click** picks up half or drops one.

A saved world (`world.sav`) is written next to the game and is loaded
automatically on the next launch.

### Console commands

Open with `T` or `/`, type a command, press `Enter`:

```
help                       list commands
seed                       show the current world seed
time day|night|<0..1>      set the time of day
tp <x> <y> <z>             teleport
give <block> [n]           add n of a block to the inventory
gamemode survival|creative switch mode
heal                       refill health
setblock <x> <y> <z> <b>   place a block
fill <x1 y1 z1 x2 y2 z2> <b>  fill a region
gamemode creative|survival creative = fly
fly                        toggle fly
speed <n>                  movement-speed multiplier
fov <n> / regen [seed]     change FOV / regenerate the world
clear                      clear the console
```

Block names: `grass dirt stone cobblestone log leaves sand planks water glass`
(or their numeric ids).

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
