# VoxelGame — a modular voxel sandbox engine

A voxel sandbox engine built **from scratch** in modern C++ (C++17), with a
clean, modular, testable architecture. No external libraries: the engine
targets Windows (32-bit and 64-bit) via MinGW-w64, and every core module also
compiles and runs headless on Linux so it can be unit-tested in isolation.

> This is a ground-up, cleanly-architected sibling of the single-file
> `MiniCraft` prototype in the repository root. It is developed **iteratively,
> one module at a time** — each module ships with unit tests that must pass
> before the next module starts.

## Architecture principles

- **SOLID.** Generation code depends on the `INoise` *interface*, not on a
  concrete noise class (DIP). fBm is a *decorator* over `INoise` (OCP). Each
  type has a single responsibility (SRP).
- **Clear folder structure.** Public headers under `include/vg/<module>/`,
  implementations under `src/<module>/`, tests under `tests/`.
- **No hidden dependencies.** Modules include only what they use; the math and
  noise layers are pure and free of platform code.
- **Everything testable.** A tiny built-in test harness (`tests/TestFramework.hpp`)
  keeps the test build dependency-free.

## Folder structure

```
voxelgame/
├── include/vg/          public headers (the module API surface)
│   ├── math/            Vec3 and geometry helpers
│   └── noise/           INoise, PerlinNoise, FractalNoise (fBm)
├── src/                 implementations
│   └── noise/
├── tests/               unit tests + the minimal test framework
├── run_tests.sh         build & run all unit tests (native, g++)
└── README.md
```

## Building & testing

Run the full unit-test suite (native):

```sh
cd voxelgame
./run_tests.sh
```

The same sources cross-compile for Windows with `x86_64-w64-mingw32-g++` and
`i686-w64-mingw32-g++`.

## Module roadmap

| # | Module | Status |
|---|--------|--------|
| 1 | Foundation: math (`Vec3`) + noise (`Perlin`, `fBm`) | ✅ done, tested |
| 2 | Block registry (hardness, tool tier, transparency, light, fluid, gravity) | ✅ done, tested |
| 3 | Chunk & world storage (infinite streaming) | ✅ done, tested |
| 4 | Terrain: biomes, caves, rivers/oceans, ores, vegetation, structures | ✅ done, tested |
| 5 | Physics & first-person controls (sprint, jump, crouch, swim, climb, collision) | ✅ done, tested |
| 6 | Rendering foundation: lighting (sky/block flood-fill) + greedy meshing | ✅ done, tested |
| 7 | Inventory & crafting (hotbar, recipes, stacks, durability, chests/furnaces) | ⏳ next |
| 8 | Creatures (passive/neutral/hostile, pathfinding, spawning) | ⏳ |
| 9 | Audio (footsteps, ambience, weather, interactions, music) | ⏳ |
| 10 | Persistence (chunk-based save/load, compression, autosave) | ⏳ |

Each row is delivered only after its unit tests pass.

## Module 1 — Foundation (done)

**`vg::math::Vec3<T>`** — a minimal templated 3D vector (`Vec3f/Vec3d/Vec3i`)
with arithmetic, `dot`/`cross`/`length`/`normalize`, negative-safe
`floorToInt`, and a `std::hash` specialization so block/chunk coordinates can
key hash maps.

**`vg::noise`** — coherent noise behind the `INoise` interface:
- `PerlinNoise` — Ken Perlin's improved gradient noise, seeded for
  reproducible worlds, output ≈ `[-1, 1]` (with `toUnit()` → `[0, 1]`).
- `FractalNoise` — fBm decorator: octaves, lacunarity, persistence; stays in
  the base range and normalizes correctly.

Tests cover determinism per seed, the zero-at-lattice property, output range,
continuity (no discontinuities), fBm reproducibility and added detail — plus
full `Vec3` arithmetic/geometry/hashing. All green.

## Module 2 — Block registry (done)

**`vg::world::BlockType`** — a value object describing a block: hardness,
preferred tool + harvest tier + whether a tool is *required* to drop,
light opacity/emission, and physics flags (`solid`, `fluid`, `gravity`,
`replaceable`) plus a `RenderLayer`. `computeMining()` turns a block + the
tool in hand into a break time and a "will it drop?" verdict (unbreakable,
correct-tool speed-up, tier gating for ores, hand-mineable blocks still drop).

**`vg::world::BlockRegistry`** — the single source of truth: dense id→type
lookup, unknown ids resolve to air (total lookups), and `registerDefaultBlocks`
adds the standard set (air, stone, dirt, grass, sand/gravel with gravity,
bedrock unbreakable, logs/planks/leaves/glass, water/lava fluids, coal/iron/
gold/diamond ores with tier gates, and a light-emitting torch). Adding blocks
never touches existing code (Open/Closed).

Tests cover registration/density, air fallback, physics/light/render flags,
and the full mining matrix (unbreakable, tool speed-up, tier gating, hand
drops). All green.

## Module 3 — Chunk & world storage (done)

**`vg::world::Chunk`** — pure storage for a `16×16×128` column of blocks, with
bounds-checked local get/set, a dirty flag (player edits) and a generated
flag. Nothing about generation or rendering lives here (SRP).

**`vg::world::World`** — owns loaded chunks in a hash map keyed by `ChunkCoord`
and exposes a flat world-space block API. `floorDiv`/`floorMod`/`worldToChunk`
handle negative coordinates correctly. `updateStreaming(cx, cz, radius)` loads
the square region around a centre and unloads everything outside it, so the
world is effectively infinite in X/Z. Terrain content comes from an injected
`IChunkGenerator` (DIP) — `FlatChunkGenerator` is the built-in/test one; the
Module 4 terrain generator will drop straight in.

Tests cover negative-coordinate math, chunk get/set bounds, cross-chunk and
negative world access, generator-fills-new-chunks, and streaming load/unload
counts while moving. All green.

## Module 4 — Terrain generation (done)

**`vg::world::TerrainGenerator`** — an `IChunkGenerator` that produces the world
deterministically from a seed and *independently per chunk* (a chunk depends
only on its coordinates + seed, so generation order never matters). Built on
the Module 1 noise behind the `INoise` interface (DIP):

- **Biomes** (`vg::world::Biome`) from temperature + humidity + elevation:
  ocean, plains, forest, desert, mountains, snowy — each with its own surface
  blocks and tree density.
- **Height & mountains** from fBm elevation with a separate peak field.
- **Oceans, lakes & rivers** — water fills up to sea level; a river noise cuts
  channels that flood.
- **Caves** carved by a 3D noise field crossing zero (winding tunnels).
- **Ores** placed in stone by depth bands + per-voxel hash (coal→iron→gold→
  diamond, diamonds only deep).
- **Trees** per biome (dense forest, sparse plains, none in desert/ocean),
  kept in the chunk interior so canopies never cross a border.
- **Structures** — a rare underground room as a worked example; elaborate
  multi-chunk structures (villages) are a future feature on the same hook.

Tests: determinism, ground+sky, biome variety + land/water, caves exist, ores
appear only where they should (diamonds only below y=14), and trees appear in
forests but never on desert sand. `tools/visualize.cpp` renders a top-down
biome map and a vertical cross-section (caves/ores/water) to PNG.

## Module 5 — Physics & first-person controls (done)

**`vg::physics::PlayerController`** — moves a player-sized AABB through the
voxel world with per-axis collide-and-resolve. It reads block data from the
`World` and block properties from the `BlockRegistry` (no rendering/input
coupling). Supports:

- **Walking / sprinting / crouching** at different speeds, movement relative
  to the look yaw.
- **Jumping** (only when grounded) with gravity and a terminal velocity.
- **Swimming** — buoyant, reduced-gravity motion when the box is in a fluid.
- **Climbing** — clinging to climbable blocks (a `Ladder` block was added to
  the registry, with a `climbable` flag on `BlockType`).
- **Sneak edge protection** — crouching while grounded cancels a horizontal
  step that would leave the player over empty space (you can lean, not fall).

Tests: gravity settling, walking + wall stop, sprint > walk, jump rise+land,
swimming slows the fall, ladder climbing, and sneak-doesn't-fall (vs. walking
which does). All green; cross-compiles win32/win64.

## Module 6 — Rendering foundation: lighting + greedy meshing (done)

The two algorithmic, headless-testable cores of the renderer. (The actual
window/rasterizer is platform code and lands in a later app module.)

**`vg::render::LightEngine`** — per-chunk lighting with two channels:
- **Skylight** cast straight down each column (dimming through translucent
  blocks) then flooded sideways with a BFS that loses one level per step, so
  light bleeds under overhangs — the basis for smooth lighting.
- **Block light** flooded the same way from every emitter (torch = 14,
  lava = 15). `combined()` mixes them with a day/night factor.

**`vg::render::Mesher`** — greedy meshing: emits only *exposed* faces (a face
is skipped when the neighbour fully occludes it) and merges coplanar same-block
faces into the largest rectangles. A solid cube becomes 6 quads instead of
6·N² ; two adjacent blocks become 6 quads (area 10) with no interior faces.

Tests: skylight full/blocked/bleeding, torch falloff, range clamping; single
block = 6 faces, cube merges to 6 quads, no interior faces, transparent
neighbours don't occlude, full layer merges. All green; cross-compiles
win32/win64.
