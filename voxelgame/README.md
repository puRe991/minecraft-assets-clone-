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
| 2 | Block registry (hardness, tool tier, transparency, light, fluid, gravity) | ⏳ next |
| 3 | Chunk & world storage (infinite streaming) | ⏳ |
| 4 | Terrain: biomes, caves & ravines, rivers/oceans, ores, vegetation, structures | ⏳ |
| 5 | Physics & first-person controls (sprint, jump, crouch, swim, climb, collision) | ⏳ |
| 6 | Rendering & lighting (sunlight, dynamic lights, smooth lighting) | ⏳ |
| 7 | Inventory & crafting (hotbar, recipes, stacks, durability, chests/furnaces) | ⏳ |
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
