/*
 * MiniCraft - a from-scratch voxel sandbox game for Windows (32-bit & 64-bit).
 *
 * Original work. Uses only the Win32 API + GDI (no external libraries, no
 * copyrighted assets). All block textures are generated procedurally at
 * runtime. Rendering is a software voxel raycaster (Amanatides & Woo DDA).
 *
 * The raycaster is the whole cost of a frame, so it gets three things:
 * rays are clipped to the world box, empty space is skipped a macro cell at a
 * time via a coarse occupancy grid, and scanlines are split across a pool of
 * worker threads. If a machine still cannot keep up, the internal resolution
 * drops automatically and the result is upscaled to the window.
 *
 * Controls:
 *   Mouse            look around
 *   W / A / S / D    move
 *   Space            jump  (fly up when flying)
 *   Left Shift       sneak / fly down
 *   F                toggle fly mode
 *   Left mouse       break block
 *   Right mouse      place block
 *   1..9, 0          select block to place
 *   K / L            save / load the world (world.sav)
 *   R                regenerate the world
 *   T                toggle adaptive resolution
 *   + / -            raise / lower render resolution (locks it)
 *   Esc              quit
 *
 * Build: see build.sh (cross-compiled with MinGW-w64 for i686 and x86_64).
 */

#ifndef HEADLESS_TEST
#include <windows.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "png.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ----------------------------------------------------------------------- */
/* Configuration                                                            */
/* ----------------------------------------------------------------------- */

#define WORLD_X 128
#define WORLD_Y 64
#define WORLD_Z 128
#define WORLD_CELLS (WORLD_X * WORLD_Y * WORLD_Z)
#define WATER_LEVEL 22

#define RENDER_W 480          /* internal render resolution (upscaled)      */
#define RENDER_H 270
#define WIN_W 960             /* default window size                        */
#define WIN_H 540

#define FOV_DEG 70.0f
#define MAX_RAY 96.0f
#define REACH 6.0f            /* block interaction distance                 */

#define TEX 16                /* texture size (px)                          */

/* Side of a macro cell in the coarse occupancy grid used to skip empty space
   while raycasting. Must divide all three world dimensions. */
#define MACRO 8
#define MACRO_X (WORLD_X / MACRO)
#define MACRO_Y (WORLD_Y / MACRO)
#define MACRO_Z (WORLD_Z / MACRO)
typedef char macro_divides_world[(WORLD_X % MACRO == 0 && WORLD_Y % MACRO == 0 &&
                                  WORLD_Z % MACRO == 0) ? 1 : -1];

#define MAX_THREADS 16        /* upper bound on render worker threads        */

/* The item catalogue and everything built on it. A block id in the world *is*
   an item id, so every placeable entry of the catalogue can be built with. */
#include "items.h"
#include "models.h"
#include "anim.h"
#include "render3d.h"
#include "font.h"

#define HOTBAR_SLOTS 10
#define MAX_DROPS    64       /* item entities lying in the world           */
#define PICKUP_RANGE 1.6f

/* ----------------------------------------------------------------------- */
/* Globals                                                                  */
/* ----------------------------------------------------------------------- */

static uint16_t g_world[WORLD_X * WORLD_Y * WORLD_Z];

/* Coarse occupancy grid: g_macro[c] is nonzero when macro cell c contains at
   least one ray-stopping block. A zero cell is guaranteed empty, which lets
   the raycaster jump MACRO blocks at a time through open air and sky. */
static uint8_t  g_macro[MACRO_X * MACRO_Y * MACRO_Z];
static int      g_macro_defer = 0;   /* bulk world edit in progress          */

static uint32_t g_framebuf[RENDER_W * RENDER_H];

/* Current internal render size. Never exceeds RENDER_W/RENDER_H; the adaptive
   resolution controller lowers it when frames get expensive. Rows are packed
   at stride g_rw. */
static int g_rw = RENDER_W, g_rh = RENDER_H;

/* Per-pixel distance from the raycast pass. Item entities are rasterised
   afterwards and depth-test against it, so a dropped item is hidden by the
   terrain in front of it. */
static float g_depth[RENDER_W * RENDER_H];

static float g_px = WORLD_X * 0.5f, g_py = 40.0f, g_pz = WORLD_Z * 0.5f;
static float g_vx = 0, g_vy = 0, g_vz = 0;
static float g_yaw = 0.0f, g_pitch = 0.0f;
static int   g_onground = 0;
static int   g_fly = 0;

/* Free-running clock that drives every animation and texture animation. */
static float g_time = 0.0f;

/* Hotbar: ten catalogue entries, one of them selected. */
static int g_hotbar[HOTBAR_SLOTS];
static int g_slot = 0;
#define g_selected (g_hotbar[g_slot])

/* The one-shot clip currently playing on the held item. */
static int   g_use_clip = ANIM_NONE;
static float g_use_time = 0.0f;

/* Item entities: what a broken block leaves behind. */
typedef struct {
    int   item;
    float x, y, z, vy;
    float age;
    int   alive;
} Drop;
static Drop g_drops[MAX_DROPS];
static int  g_drop_count = 0;

static int   g_keys[256];
static int   g_running = 1;
static int   g_focused = 1;

#ifndef HEADLESS_TEST
static HWND  g_hwnd;
#endif
static int   g_client_w = WIN_W, g_client_h = WIN_H;

/* edge-triggered mouse actions handled in the message loop */
static volatile int g_break_req = 0;
static volatile int g_place_req = 0;

/* ----------------------------------------------------------------------- */
/* World access                                                             */
/* ----------------------------------------------------------------------- */

static inline int in_world(int x, int y, int z) {
    return x >= 0 && x < WORLD_X && y >= 0 && y < WORLD_Y && z >= 0 && z < WORLD_Z;
}
static inline uint16_t get_block(int x, int y, int z) {
    if (!in_world(x, y, z)) return IT_AIR;
    return g_world[(y * WORLD_Z + z) * WORLD_X + x];
}
/* Anything that isn't air or a liquid may stop a ray. Partial models are
   included: the macro grid has to be conservative, and the raycaster then
   intersects the model's boxes for the exact answer. */
static inline int stops_ray(int b) {
    return b != IT_AIR && !item_is_liquid(b);
}

/* Recompute one macro cell from the blocks it covers. */
static void macro_rebuild_cell(int mx, int my, int mz) {
    int occupied = 0;
    for (int y = my * MACRO; y < my * MACRO + MACRO && !occupied; y++)
        for (int z = mz * MACRO; z < mz * MACRO + MACRO && !occupied; z++)
            for (int x = mx * MACRO; x < mx * MACRO + MACRO; x++)
                if (stops_ray(g_world[(y * WORLD_Z + z) * WORLD_X + x])) { occupied = 1; break; }
    g_macro[(my * MACRO_Z + mz) * MACRO_X + mx] = (uint8_t)occupied;
}

static void macro_rebuild_all(void) {
    for (int my = 0; my < MACRO_Y; my++)
        for (int mz = 0; mz < MACRO_Z; mz++)
            for (int mx = 0; mx < MACRO_X; mx++)
                macro_rebuild_cell(mx, my, mz);
}

static inline void set_block(int x, int y, int z, uint16_t v) {
    if (!in_world(x, y, z)) return;
    g_world[(y * WORLD_Z + z) * WORLD_X + x] = v;
    if (g_macro_defer) return;                 /* caller rebuilds afterwards */
    int m = ((y / MACRO) * MACRO_Z + (z / MACRO)) * MACRO_X + (x / MACRO);
    if (stops_ray(v)) g_macro[m] = 1;          /* marking is always safe     */
    else if (g_macro[m]) macro_rebuild_cell(x / MACRO, y / MACRO, z / MACRO);
}

/* "Is there anything to stand on here" -- a cheap per-cell test. The exact
   shape is only consulted by collide(), which tests the model's boxes. */
static inline int is_solid(int x, int y, int z) {
    return item_is_solid(get_block(x, y, z));
}

/* ----------------------------------------------------------------------- */
/* Procedural noise                                                         */
/* ----------------------------------------------------------------------- */

static unsigned hash2(int x, int y) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float rnd2(int x, int y) { return (hash2(x, y) & 0xffffff) / (float)0xffffff; }

static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

static float valnoise(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    float a = rnd2(xi, yi),     b = rnd2(xi + 1, yi);
    float c = rnd2(xi, yi + 1), d = rnd2(xi + 1, yi + 1);
    float u = smooth(xf), v = smooth(yf);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

static float fbm(float x, float y) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int o = 0; o < 4; o++) {
        sum += amp * valnoise(x * freq, y * freq);
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return sum / norm;
}

/* ----------------------------------------------------------------------- */
/* World generation                                                         */
/* ----------------------------------------------------------------------- */

static int collide(float ex, float ey, float ez);   /* forward decl */

/* Which wood a tree is made of, so the world shows off more than one entry of
   the catalogue. Keyed off position, so a given seed is reproducible. */
static const uint16_t g_tree_kinds[5][2] = {
    { IT_OAK_LOG,     IT_OAK_LEAVES     },
    { IT_BIRCH_LOG,   IT_BIRCH_LEAVES   },
    { IT_SPRUCE_LOG,  IT_SPRUCE_LEAVES  },
    { IT_ACACIA_LOG,  IT_ACACIA_LEAVES  },
    { IT_CHERRY_LOG,  IT_CHERRY_LEAVES  },
};

/* Scattered ground cover, drawn with the cross model. */
static const uint16_t g_flora[8] = {
    IT_SHORT_GRASS_GRASS, IT_POPPY, IT_DANDELION, IT_CORNFLOWER,
    IT_AZURE_BLUET, IT_OXEYE_DAISY, IT_ALLIUM, IT_FERN,
};

static void place_tree(int x, int z, int ground) {
    int kind = (int)(rnd2(x * 31 + 7, z * 19 + 3) * 5.0f);
    if (kind > 4) kind = 4;
    uint16_t log = g_tree_kinds[kind][0], leaf = g_tree_kinds[kind][1];
    int h = 4 + (int)(rnd2(x * 7, z * 3) * 3.0f);
    for (int i = 1; i <= h; i++) set_block(x, ground + i, z, log);
    int top = ground + h;
    for (int dy = -1; dy <= 2; dy++) {
        int r = (dy <= 0) ? 2 : 1;
        for (int dx = -r; dx <= r; dx++)
            for (int dz = -r; dz <= r; dz++) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (abs(dx) == r && abs(dz) == r && (rnd2(x + dx, z + dz) < 0.4f)) continue;
                int yy = top + dy;
                if (get_block(x + dx, yy, z + dz) == IT_AIR)
                    set_block(x + dx, yy, z + dz, leaf);
            }
    }
}

static void gen_world(unsigned seed) {
    for (unsigned i = 0; i < WORLD_CELLS; i++) g_world[i] = IT_AIR;
    g_macro_defer = 1;                 /* one rebuild at the end, not 1M */
    float ox = (seed % 997) * 1.3f, oz = (seed % 733) * 1.7f;

    for (int x = 0; x < WORLD_X; x++) {
        for (int z = 0; z < WORLD_Z; z++) {
            float n = fbm((x + ox) * 0.045f, (z + oz) * 0.045f);
            int h = (int)(14 + n * 30);
            if (h < 1) h = 1;
            if (h >= WORLD_Y) h = WORLD_Y - 1;
            for (int y = 0; y <= h; y++) {
                uint16_t b;
                if (y == h) {
                    if (h <= WATER_LEVEL + 1) b = IT_SAND;
                    else b = IT_GRASS_BLOCK;
                } else if (y >= h - 3) {
                    b = (h <= WATER_LEVEL + 1) ? IT_SAND : IT_DIRT;
                } else {
                    b = IT_STONE;
                }
                set_block(x, y, z, b);
            }
            /* water fill */
            for (int y = h + 1; y <= WATER_LEVEL; y++) set_block(x, y, z, IT_WATER);
        }
    }
    /* trees and ground cover on grass above water */
    for (int x = 3; x < WORLD_X - 3; x++)
        for (int z = 3; z < WORLD_Z - 3; z++) {
            /* find surface */
            int y = WORLD_Y - 1;
            while (y > 0 && get_block(x, y, z) == IT_AIR) y--;
            if (get_block(x, y, z) != IT_GRASS_BLOCK || y <= WATER_LEVEL + 1)
                continue;
            float r = rnd2(x * 13 + 1, z * 17 + 5);
            if (r < 0.018f)
                place_tree(x, z, y);
            else if (r > 0.90f && get_block(x, y + 1, z) == IT_AIR)
                set_block(x, y + 1, z,
                          g_flora[(int)(rnd2(x * 5 + 3, z * 11 + 9) * 8.0f) & 7]);
        }

    g_macro_defer = 0;
    macro_rebuild_all();

    /* spawn on top of terrain at center */
    int cx = WORLD_X / 2, cz = WORLD_Z / 2, cy = WORLD_Y - 1;
    while (cy > 0 && get_block(cx, cy, cz) == B_AIR) cy--;
    g_px = cx + 0.5f; g_pz = cz + 0.5f; g_py = cy + 2.6f;
    /* ensure we don't spawn inside overhanging leaves/terrain */
    for (int guard = 0; guard < WORLD_Y && collide(g_px, g_py, g_pz); guard++)
        g_py += 1.0f;
    g_vx = g_vy = g_vz = 0; g_onground = 0;
}

/* ----------------------------------------------------------------------- */
/* Textures                                                                 */
/*                                                                          */
/* Every catalogue entry's texture is synthesised at startup from its        */
/* pattern and colours (see items.h). A real Minecraft-format resource pack  */
/* still wins where one exists: load_assets() slugifies the item name and    */
/* looks for assets/minecraft/textures/{block,item}/<name>{,_top,_side,      */
/* _bottom}.png, so dropping a pack next to the executable replaces as much  */
/* of the generated art as the pack happens to cover.                       */
/* ----------------------------------------------------------------------- */

static void gen_textures(void) { build_item_textures(); }

static int g_assets_loaded = 0;   /* number of texture slots loaded from disk */

/* "Grass Block" -> "grass_block"; "Bottle o' Enchanting" -> "bottle_o_enchanting" */
static void item_slug(int id, char *out, size_t n) {
    const char *s = item_name(id);
    size_t o = 0;
    int prev_us = 1;
    for (; *s && o + 1 < n; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[o++] = c; prev_us = 0;
        } else if (!prev_us) {
            out[o++] = '_'; prev_us = 1;
        }
    }
    while (o > 0 && out[o - 1] == '_') o--;
    out[o] = 0;
}

/* Loads one PNG into a texture layer, scaling it to 16x16 and taking the
   first frame of an animated strip. Returns 1 on success. */
static int load_layer(const char *path, uint32_t *dst, uint32_t tint) {
    int w, h;
    uint32_t *img = png_load(path, &w, &h);
    if (!img) return 0;
    int frame_h = (h >= w) ? w : h;       /* animated strips: use first frame */
    if (frame_h < 1) frame_h = 1;
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
            int sx = x * w / TEX, sy = y * frame_h / TEX;
            if (sx >= w) sx = w - 1;
            if (sy >= h) sy = h - 1;
            uint32_t c = img[sy * w + sx];
            if (tint) {
                int tr = (tint >> 16) & 0xff, tg = (tint >> 8) & 0xff, tb = tint & 0xff;
                int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
                c = (c & 0xff000000u) | (uint32_t)(((r * tr) / 255) << 16) |
                    (uint32_t)(((g * tg) / 255) << 8) | (uint32_t)((b * tb) / 255);
            }
            dst[y * TEX + x] = c;
        }
    free(img);
    return 1;
}

/* Does a pack live under `base`? Probing a handful of names is much cheaper
   than letting 1712 items each miss on three candidate directories. */
static int pack_present(const char *base) {
    static const char *probe[] = { "stone", "dirt", "sand", "cobblestone",
                                   "oak_planks", "glass", "grass_block_top" };
    for (unsigned i = 0; i < sizeof probe / sizeof probe[0]; i++) {
        char path[700];
        snprintf(path, sizeof path,
                 "%sassets/minecraft/textures/block/%s.png", base, probe[i]);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return 1; }
    }
    return 0;
}

static void load_assets(void) {
    const char *bases[4];
    int nb = 0;
#ifndef HEADLESS_TEST
    static char exedir[600];
    DWORD n = GetModuleFileNameA(NULL, exedir, sizeof exedir);
    if (n > 0 && n < sizeof exedir) {
        for (int i = (int)n - 1; i >= 0; i--)
            if (exedir[i] == '\\' || exedir[i] == '/') { exedir[i + 1] = 0; break; }
        bases[nb++] = exedir;
    }
#endif
    bases[nb++] = "";       /* current working directory */
    bases[nb++] = "../";    /* running from dist/ next to repo root */

    g_assets_loaded = 0;
    const char *base = NULL;
    for (int i = 0; i < nb && !base; i++)
        if (pack_present(bases[i])) base = bases[i];
    if (!base) return;                       /* no pack: keep the generated art */

    static const char *suffix[3] = { "_top", "_side", "_bottom" };
    for (int id = 1; id < ITEM_COUNT; id++) {
        const ItemDef *d = &g_items[id];
        if (d->flags & IF_HIDDEN) continue;
        const char *sub = item_is_placeable(id) ? "block" : "item";
        char slug[128];
        item_slug(id, slug, sizeof slug);
        if (!slug[0]) continue;

        /* Minecraft's grass and foliage textures are greyscale and tinted by
           biome; using the catalogue colour as the tint keeps a stock pack
           looking right. */
        uint32_t tint = 0;
        if (d->pattern == PAT_FOLIAGE || id == IT_GRASS_BLOCK)
            tint = d->col_side & 0x00ffffffu;

        for (int face = 0; face < 3; face++) {
            char path[700];
            uint32_t px[TEX * TEX];
            snprintf(path, sizeof path, "%sassets/minecraft/textures/%s/%s%s.png",
                     base, sub, slug, suffix[face]);
            if (!load_layer(path, px, tint)) {
                snprintf(path, sizeof path, "%sassets/minecraft/textures/%s/%s.png",
                         base, sub, slug);
                if (!load_layer(path, px, tint)) continue;
            }
            /* Layers are shared between items that resolved to the same
               material, so a pack texture has to land in a private copy or it
               would repaint every one of them. */
            uint16_t layer = layer_private(id, face);
            if (!layer) continue;                 /* pool exhausted */
            memcpy(g_layers[layer], px, sizeof px);
            g_assets_loaded++;
        }
    }
}

/* ----------------------------------------------------------------------- */
/* World save / load (simple RLE-compressed save file)                      */
/* ----------------------------------------------------------------------- */

/* Block ids used to be 8 bit, when the game had eleven blocks. They are item
   ids now, so the save format carries 16 bits per run -- but a "MCW1" file
   from the old build still loads, through this table. */
static const uint16_t g_legacy_ids[11] = {
    IT_AIR, IT_GRASS_BLOCK, IT_DIRT, IT_STONE, IT_COBBLESTONE, IT_OAK_LOG,
    IT_OAK_LEAVES, IT_SAND, IT_OAK_PLANKS, IT_WATER, IT_GLASS,
};

static int save_world(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("MCW2", 1, 4, f);
    int dims[3] = {WORLD_X, WORLD_Y, WORLD_Z};
    fwrite(dims, sizeof(int), 3, f);
    float ps[5] = {g_px, g_py, g_pz, g_yaw, g_pitch};
    fwrite(ps, sizeof(float), 5, f);
    fwrite(g_hotbar, sizeof(int), HOTBAR_SLOTS, f);
    unsigned i = 0, total = WORLD_CELLS;
    while (i < total) {                     /* run-length encode block ids */
        uint16_t v = g_world[i];
        unsigned run = 1;
        while (i + run < total && g_world[i + run] == v && run < 0xffffffu) run++;
        fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
        fputc(run & 0xff, f); fputc((run >> 8) & 0xff, f); fputc((run >> 16) & 0xff, f);
        i += run;
    }
    fclose(f);
    return 1;
}

static int load_world(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4]; int dims[3]; float ps[5];
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return 0; }
    int v2 = memcmp(magic, "MCW2", 4) == 0;
    int v1 = memcmp(magic, "MCW1", 4) == 0;
    if (!v1 && !v2) { fclose(f); return 0; }
    if (fread(dims, sizeof(int), 3, f) != 3 ||
        dims[0] != WORLD_X || dims[1] != WORLD_Y || dims[2] != WORLD_Z) { fclose(f); return 0; }
    if (fread(ps, sizeof(float), 5, f) != 5) { fclose(f); return 0; }
    int hotbar[HOTBAR_SLOTS];
    int have_hotbar = v2 && fread(hotbar, sizeof(int), HOTBAR_SLOTS, f) == HOTBAR_SLOTS;

    unsigned i = 0, total = WORLD_CELLS;
    while (i < total) {
        int lo = fgetc(f);
        int hi = v2 ? fgetc(f) : 0;
        int b0 = fgetc(f), b1 = fgetc(f), b2 = fgetc(f);
        if (lo < 0 || hi < 0 || b2 < 0) break;
        unsigned id = (unsigned)lo | ((unsigned)hi << 8);
        if (v1) id = (id < 11) ? g_legacy_ids[id] : IT_AIR;
        if (id >= ITEM_COUNT) id = IT_AIR;
        unsigned run = (unsigned)b0 | ((unsigned)b1 << 8) | ((unsigned)b2 << 16);
        while (run-- && i < total) g_world[i++] = (uint16_t)id;
    }
    fclose(f);
    macro_rebuild_all();            /* g_world was written directly, even on a
                                       short read, so resync the macro grid */
    if (i != total) return 0;
    g_px = ps[0]; g_py = ps[1]; g_pz = ps[2]; g_yaw = ps[3]; g_pitch = ps[4];
    g_vx = g_vy = g_vz = 0; g_onground = 0;
    if (have_hotbar)
        for (int s = 0; s < HOTBAR_SLOTS; s++)
            if (hotbar[s] > 0 && hotbar[s] < ITEM_COUNT && item_is_placeable(hotbar[s]))
                g_hotbar[s] = hotbar[s];
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Camera / rays                                                            */
/* ----------------------------------------------------------------------- */

typedef struct { float x, y, z; } V3;

static void camera_basis(V3 *fwd, V3 *right, V3 *up) {
    float cy = cosf(g_yaw), sy = sinf(g_yaw);
    float cp = cosf(g_pitch), sp = sinf(g_pitch);
    fwd->x = cp * sy; fwd->y = sp; fwd->z = cp * cy;
    /* right = normalize(cross(fwd, worldUp)) */
    right->x = cy; right->y = 0; right->z = -sy;
    /* up = cross(right, fwd) */
    up->x = right->y * fwd->z - right->z * fwd->y;
    up->y = right->z * fwd->x - right->x * fwd->z;
    up->z = right->x * fwd->y - right->y * fwd->x;
}

/* DDA raycast (Amanatides & Woo). Returns 1 on hit, filling the block coords,
   the face normal, the distance and the block id.
 *
 * Three things keep this cheap, since it runs once per pixel:
 *   1. the ray is clipped to the world box up front, so a ray aimed at the sky
 *      stops at the world boundary instead of stepping all the way to maxdist;
 *   2. traversal is hierarchical -- an outer DDA walks the macro grid and skips
 *      MACRO blocks at a stride whenever a macro cell is known to be empty;
 *   3. the inner DDA is confined to one macro cell, so its block reads are
 *      in-bounds by construction and need no per-step bounds check.
 * The visible result is identical to a plain per-block DDA. */
static int raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                   float maxdist, int *hx, int *hy, int *hz,
                   int *nx, int *ny, int *nz, float *outdist, int *outblock) {
    /* signed inverses (0 stands in for "never crosses"; guarded by the ddN
       sentinel below) and the classic per-axis DDA constants */
    float ix = (dx == 0.0f) ? 0.0f : 1.0f / dx;
    float iy = (dy == 0.0f) ? 0.0f : 1.0f / dy;
    float iz = (dz == 0.0f) ? 0.0f : 1.0f / dz;
    float ddx = (dx == 0.0f) ? 1e30f : fabsf(ix);
    float ddy = (dy == 0.0f) ? 1e30f : fabsf(iy);
    float ddz = (dz == 0.0f) ? 1e30f : fabsf(iz);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, sz = dz < 0 ? -1 : 1;

    /* --- clip the ray to the world box --- */
    float t0 = 0.0f, t1 = maxdist, ta, tb;
    int entry_axis = -1;              /* face the ray enters the box by, if any */
    int inside = (ox >= 0.0f && ox < (float)WORLD_X && oy >= 0.0f && oy < (float)WORLD_Y &&
                  oz >= 0.0f && oz < (float)WORLD_Z);
    if (inside) {
        /* Fast path -- the camera is in the world, so only the exit matters.
           This is the case for every one of the ~130k rays cast per frame. */
        if (dx != 0.0f) { ta = ((dx > 0 ? (float)WORLD_X : 0.0f) - ox) * ix; if (ta < t1) t1 = ta; }
        if (dy != 0.0f) { ta = ((dy > 0 ? (float)WORLD_Y : 0.0f) - oy) * iy; if (ta < t1) t1 = ta; }
        if (dz != 0.0f) { ta = ((dz > 0 ? (float)WORLD_Z : 0.0f) - oz) * iz; if (ta < t1) t1 = ta; }
    } else {
        if (dx == 0.0f) { if (ox < 0.0f || ox >= (float)WORLD_X) return 0; }
        else { ta = (0.0f - ox) * ix; tb = ((float)WORLD_X - ox) * ix;
               if (ta > tb) { float s = ta; ta = tb; tb = s; }
               if (ta > t0) { t0 = ta; entry_axis = 0; }
               if (tb < t1) t1 = tb;
               if (t0 > t1) return 0; }
        if (dy == 0.0f) { if (oy < 0.0f || oy >= (float)WORLD_Y) return 0; }
        else { ta = (0.0f - oy) * iy; tb = ((float)WORLD_Y - oy) * iy;
               if (ta > tb) { float s = ta; ta = tb; tb = s; }
               if (ta > t0) { t0 = ta; entry_axis = 1; }
               if (tb < t1) t1 = tb;
               if (t0 > t1) return 0; }
        if (dz == 0.0f) { if (oz < 0.0f || oz >= (float)WORLD_Z) return 0; }
        else { ta = (0.0f - oz) * iz; tb = ((float)WORLD_Z - oz) * iz;
               if (ta > tb) { float s = ta; ta = tb; tb = s; }
               if (ta > t0) { t0 = ta; entry_axis = 2; }
               if (tb < t1) t1 = tb;
               if (t0 > t1) return 0; }
    }
    if (t1 <= 0.0f) return 0;

    int mx, my, mz;               /* current block cell                       */
    float sdx, sdy, sdz;          /* absolute t of the next crossing per axis  */
    float t = t0;                 /* t at which the current cell was entered   */

    /* (Re)start the block DDA from the point at parameter t. Used once at the
       world entry point and again after every jump over empty space.
       The clamp makes a truncating cast equivalent to floorf() here, which
       matters because floorf() is an out-of-line call on the 32-bit target. */
#define SEED_DDA() do {                                                        \
        float px = ox + dx * t, py = oy + dy * t, pz = oz + dz * t;            \
        mx = (int)px; if (mx < 0) mx = 0;                                      \
        else if (mx >= WORLD_X) mx = WORLD_X - 1;                              \
        my = (int)py; if (my < 0) my = 0;                                      \
        else if (my >= WORLD_Y) my = WORLD_Y - 1;                              \
        mz = (int)pz; if (mz < 0) mz = 0;                                      \
        else if (mz >= WORLD_Z) mz = WORLD_Z - 1;                              \
        sdx = (dx < 0 ? (px - mx) : (mx + 1 - px)) * ddx + t;                  \
        sdy = (dy < 0 ? (py - my) : (my + 1 - py)) * ddy + t;                  \
        sdz = (dz < 0 ? (pz - mz) : (mz + 1 - pz)) * ddz + t;                  \
    } while (0)

    SEED_DDA();

    /* A cell is entered through the face named by `side`; -1 marks the cell the
       ray starts in, which stays untested so a camera stuck inside a block can
       still see out (the original behaviour). */
    int side = entry_axis;
    int check_macro = 1;

    /* index of the first block of a macro cell along each axis, in the
       direction of travel -- reaching it means a new macro cell was entered */
    const int edge_x = (sx > 0) ? 0 : MACRO - 1;
    const int edge_y = (sy > 0) ? 0 : MACRO - 1;
    const int edge_z = (sz > 0) ? 0 : MACRO - 1;

    for (;;) {
        /* --- 1. jump over runs of empty macro cells --- */
        if (check_macro &&
            !g_macro[((my / MACRO) * MACRO_Z + (mz / MACRO)) * MACRO_X + (mx / MACRO)]) {
            int mcx = mx / MACRO, mcy = my / MACRO, mcz = mz / MACRO;
            float tmx = (dx == 0.0f) ? 1e30f : ((float)((dx > 0 ? mcx + 1 : mcx) * MACRO) - ox) * ix;
            float tmy = (dy == 0.0f) ? 1e30f : ((float)((dy > 0 ? mcy + 1 : mcy) * MACRO) - oy) * iy;
            float tmz = (dz == 0.0f) ? 1e30f : ((float)((dz > 0 ? mcz + 1 : mcz) * MACRO) - oz) * iz;
            do {
                if (tmx < tmy && tmx < tmz) {
                    t = tmx; if (t >= t1) return 0;
                    mcx += sx; if ((unsigned)mcx >= MACRO_X) return 0;
                    tmx += MACRO * ddx; side = 0;
                } else if (tmy < tmz) {
                    t = tmy; if (t >= t1) return 0;
                    mcy += sy; if ((unsigned)mcy >= MACRO_Y) return 0;
                    tmy += MACRO * ddy; side = 1;
                } else {
                    t = tmz; if (t >= t1) return 0;
                    mcz += sz; if ((unsigned)mcz >= MACRO_Z) return 0;
                    tmz += MACRO * ddz; side = 2;
                }
            } while (!g_macro[(mcy * MACRO_Z + mcz) * MACRO_X + mcx]);
            SEED_DDA();      /* land on the first block of the occupied cell */
        }
        check_macro = 0;

        /* --- 2. test the cell we are in --- */
        if (side >= 0) {
            uint16_t blk = g_world[(my * WORLD_Z + mz) * WORLD_X + mx];
            if (stops_ray(blk)) {
                if (item_is_full_cube(blk)) {
                    *hx = mx; *hy = my; *hz = mz;
                    *nx = *ny = *nz = 0;
                    if (side == 0)      *nx = -sx;
                    else if (side == 1) *ny = -sy;
                    else                *nz = -sz;
                    if (outdist)  *outdist  = t;
                    if (outblock) *outblock = blk;
                    return 1;
                }
                /* Partial model: intersect its boxes. A miss (the ray passed
                   through the gap beside a torch, or through a transparent
                   texel of a plant) just continues the traversal. */
                float bt; int bnx, bny, bnz;
                if (model_ray_hit(blk, mx, my, mz, ox, oy, oz, ix, iy, iz,
                                  t1, &bt, &bnx, &bny, &bnz)) {
                    *hx = mx; *hy = my; *hz = mz;
                    *nx = bnx; *ny = bny; *nz = bnz;
                    if (outdist)  *outdist  = bt;
                    if (outblock) *outblock = blk;
                    return 1;
                }
            }
        }

        /* --- 3. advance one block --- */
        if (sdx < sdy && sdx < sdz) {
            t = sdx; if (t >= t1) return 0;
            sdx += ddx; mx += sx; if ((unsigned)mx >= WORLD_X) return 0;
            side = 0; check_macro = ((mx & (MACRO - 1)) == edge_x);
        } else if (sdy < sdz) {
            t = sdy; if (t >= t1) return 0;
            sdy += ddy; my += sy; if ((unsigned)my >= WORLD_Y) return 0;
            side = 1; check_macro = ((my & (MACRO - 1)) == edge_y);
        } else {
            t = sdz; if (t >= t1) return 0;
            sdz += ddz; mz += sz; if ((unsigned)mz >= WORLD_Z) return 0;
            side = 2; check_macro = ((mz & (MACRO - 1)) == edge_z);
        }
    }
#undef SEED_DDA
}

/* ----------------------------------------------------------------------- */
/* Rendering                                                                */
/* ----------------------------------------------------------------------- */

/* Fractional part of a value known to be non-negative. Avoids floorf(), which
   is an out-of-line libm call on the 32-bit target and runs twice per pixel. */
static inline float fracp(float v) { return v - (float)(int)v; }

/* Texture-animation phase for this frame, published once so every worker
   thread animates in step. */
static float g_tex_phase = 0.0f;

/* Per-frame camera state, published once before the worker threads start. */
static struct {
    V3    fwd, right, up;
    float ox, oy, oz;
    float su0, dsu, tan_v;      /* screen-space ray parameters */
    int   sky_r[RENDER_H], sky_g[RENDER_H], sky_b[RENDER_H];
} g_view;

#define SKY_BOT_R 200
#define SKY_BOT_G 225
#define SKY_BOT_B 250

/* Renders every ystep'th scanline starting at y0. Splitting the frame between
   threads by interleaving rows (rather than in contiguous bands) keeps the
   load even: sky rows cost far more than ground rows. */
static void render_rows(int y0, int ystep) {
    const int rw = g_rw, rh = g_rh;
    const float ox = g_view.ox, oy = g_view.oy, oz = g_view.oz;

    for (int y = y0; y < rh; y += ystep) {
        float sv = (1.0f - 2.0f * (y + 0.5f) / rh) * g_view.tan_v;
        /* ray direction = base + su * right, with su exact per pixel */
        const float bx = g_view.fwd.x + sv * g_view.up.x;
        const float by = g_view.fwd.y + sv * g_view.up.y;
        const float bz = g_view.fwd.z + sv * g_view.up.z;
        const float ux = g_view.right.x, uy = g_view.right.y, uz = g_view.right.z;

        const int skr = g_view.sky_r[y], skg = g_view.sky_g[y], skb = g_view.sky_b[y];
        uint32_t *row = &g_framebuf[y * rw];
        float    *drow = &g_depth[y * rw];

        for (int x = 0; x < rw; x++) {
            float su = g_view.su0 + x * g_view.dsu;
            float dx = bx + su * ux, dy = by + su * uy, dz = bz + su * uz;
            float il = 1.0f / sqrtf(dx * dx + dy * dy + dz * dz);
            float rx = dx * il, ry = dy * il, rz = dz * il;

            int hx, hy, hz, nx, ny, nz, blk;
            float dist;

            if (raycast(ox, oy, oz, rx, ry, rz, MAX_RAY,
                        &hx, &hy, &hz, &nx, &ny, &nz, &dist, &blk)) {
                float hxp = ox + rx * dist;
                float hyp = oy + ry * dist;
                float hzp = oz + rz * dist;
                float u, v;
                int face;
                if (nx != 0)      { u = fracp(hzp); v = 1 - fracp(hyp); face = 1; }
                else if (nz != 0) { u = fracp(hxp); v = 1 - fracp(hyp); face = 1; }
                else              { u = fracp(hxp); v = fracp(hzp);
                                    face = (ny > 0) ? 0 : 2; }
                int tu = (int)(u * TEX); if (tu < 0) tu = 0; else if (tu >= TEX) tu = TEX - 1;
                int tv = (int)(v * TEX); if (tv < 0) tv = 0; else if (tv >= TEX) tv = TEX - 1;
                uint32_t c = item_sample(blk, face, tu, tv, g_tex_phase);

                /* face lighting, then distance fog toward the sky colour.
                   Both factors are <= 1 and both endpoints are bytes, so the
                   result cannot leave 0..255 and needs no clamping. A block
                   that emits light lifts its own faces out of the shading. */
                float lf  = (ny > 0) ? 1.0f : (ny < 0) ? 0.55f : (nx != 0 ? 0.8f : 0.68f);
                int   lit = item_light(blk);
                if (lit) {
                    float e = 0.6f + lit * (0.4f / 15.0f);
                    if (e > lf) lf = e;
                }
                float fog = dist * (1.0f / MAX_RAY); if (fog > 1.0f) fog = 1.0f;
                fog *= 0.85f;

                int r = (int)(((c >> 16) & 0xff) * lf);
                int g = (int)(((c >>  8) & 0xff) * lf);
                int b = (int)(( c        & 0xff) * lf);
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                r += (int)((SKY_BOT_R - r) * fog);
                g += (int)((SKY_BOT_G - g) * fog);
                b += (int)((SKY_BOT_B - b) * fog);
                row[x] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                drow[x] = dist;
            } else {
                row[x] = 0xff000000u | ((uint32_t)skr << 16) |
                         ((uint32_t)skg << 8) | (uint32_t)skb;
                drow[x] = 1e30f;
            }
        }
    }
}

/* ----------------------------------------------------------------------- */
/* Item entities                                                            */
/*                                                                          */
/* Breaking a block leaves the item lying in the world, turning on the spot  */
/* (the ANIM_SPIN clip) until you walk over it.                             */
/* ----------------------------------------------------------------------- */

static void drops_clear(void) {
    for (int i = 0; i < MAX_DROPS; i++) g_drops[i].alive = 0;
    g_drop_count = 0;
}

static void drop_spawn(int item, float x, float y, float z) {
    if (item <= IT_AIR || item >= ITEM_COUNT) return;
    for (int i = 0; i < MAX_DROPS; i++) {
        if (g_drops[i].alive) continue;
        g_drops[i].item = item;
        g_drops[i].x = x; g_drops[i].y = y; g_drops[i].z = z;
        g_drops[i].vy = 0.0f;
        g_drops[i].age = 0.0f;
        g_drops[i].alive = 1;
        g_drop_count++;
        return;
    }
    /* all slots busy: replace the oldest, so the newest break is never lost */
    int oldest = 0;
    for (int i = 1; i < MAX_DROPS; i++)
        if (g_drops[i].age > g_drops[oldest].age) oldest = i;
    g_drops[oldest].item = item;
    g_drops[oldest].x = x; g_drops[oldest].y = y; g_drops[oldest].z = z;
    g_drops[oldest].vy = 0.0f; g_drops[oldest].age = 0.0f;
}

/* Puts an item into the hotbar: the current slot if it is empty or already
   holds it, otherwise the first free slot, otherwise the current slot. */
static void hotbar_take(int item) {
    if (!item_is_placeable(item)) return;
    for (int s = 0; s < HOTBAR_SLOTS; s++)
        if (g_hotbar[s] == item) { g_slot = s; return; }
    for (int s = 0; s < HOTBAR_SLOTS; s++)
        if (g_hotbar[s] == IT_AIR) { g_hotbar[s] = item; g_slot = s; return; }
    g_hotbar[g_slot] = item;
}

static void drops_update(float dt) {
    for (int i = 0; i < MAX_DROPS; i++) {
        Drop *d = &g_drops[i];
        if (!d->alive) continue;
        d->age += dt;

        d->vy -= 18.0f * dt;
        float ny = d->y + d->vy * dt;
        if (is_solid((int)floorf(d->x), (int)floorf(ny - 0.15f), (int)floorf(d->z))) {
            d->y = floorf(ny - 0.15f) + 1.15f;
            d->vy = 0.0f;
        } else {
            d->y = ny;
        }
        if (d->y < -10.0f) { d->alive = 0; g_drop_count--; continue; }

        float dx = d->x - g_px, dy = d->y - (g_py - 0.8f), dz = d->z - g_pz;
        if (d->age > 0.4f &&
            dx * dx + dy * dy + dz * dz < PICKUP_RANGE * PICKUP_RANGE) {
            hotbar_take(d->item);
            d->alive = 0;
            g_drop_count--;
        }
    }
}

/* Fills a CamView from this frame's camera, for the entity rasteriser. */
static void cam_view(CamView *c) {
    c->ox = g_view.ox; c->oy = g_view.oy; c->oz = g_view.oz;
    c->fx = g_view.fwd.x;   c->fy = g_view.fwd.y;   c->fz = g_view.fwd.z;
    c->rx = g_view.right.x; c->ry = g_view.right.y; c->rz = g_view.right.z;
    c->ux = g_view.up.x;    c->uy = g_view.up.y;    c->uz = g_view.up.z;
    c->su0 = g_view.su0; c->dsu = g_view.dsu; c->tan_v = g_view.tan_v;
    c->w = g_rw; c->h = g_rh;
}

static void render_drops(void) {
    if (!g_drop_count) return;
    CamView cam;
    cam_view(&cam);
    for (int i = 0; i < MAX_DROPS; i++) {
        Drop *d = &g_drops[i];
        if (!d->alive) continue;
        Pose p;
        anim_pose(ANIM_SPIN, d->age, &p);
        float bright = 1.0f;
        if (item_light(d->item)) bright = 1.25f;
        draw_item_world(g_framebuf, g_depth, &cam, d->item, &p,
                        d->x, d->y, d->z, 0.30f, g_tex_phase, bright);
    }
}

/* ----------------------------------------------------------------------- */
/* Inventory                                                                */
/*                                                                          */
/* 1712 entries do not fit on a screen, so the inventory is a filtered grid: */
/* type to search by name, Tab to cycle categories, PgUp/PgDn to page.       */
/* ----------------------------------------------------------------------- */

#define INV_COLS 9
#define INV_ROWS 6
#define INV_PAGE (INV_COLS * INV_ROWS)

static int  g_inv_open = 0;
static char g_inv_search[24];
static int  g_inv_search_len = 0;
static int  g_inv_cat = 0;              /* 0 = every category */
static int  g_inv_cursor = 0;           /* index into the filtered list */
static uint16_t g_inv_list[ITEM_COUNT];
static int  g_inv_count = 0;

static int ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

/* Case-insensitive substring test, so "oak pl" finds "Dark Oak Planks". */
static int name_matches(const char *name, const char *needle) {
    if (!needle[0]) return 1;
    for (const char *s = name; *s; s++) {
        const char *a = s, *b = needle;
        while (*a && *b && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b)) {
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static void inv_refilter(void) {
    g_inv_count = 0;
    for (int id = 1; id < ITEM_COUNT; id++) {
        if (g_items[id].flags & IF_HIDDEN) continue;
        if (g_inv_cat && g_items[id].category != g_inv_cat) continue;
        if (!name_matches(item_name(id), g_inv_search)) continue;
        g_inv_list[g_inv_count++] = (uint16_t)id;
    }
    if (g_inv_cursor >= g_inv_count) g_inv_cursor = g_inv_count ? g_inv_count - 1 : 0;
}

static const char *const g_cat_names[CAT_COUNT] = {
    "Engine", "Items", "Potions", "Enchanted Books", "Smithing Templates",
    "Pottery"
};

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= g_rh) continue;
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= g_rw) continue;
            g_framebuf[py * g_rw + px] = c;
        }
    }
}

/* Blends a colour over a rectangle, for the inventory's dimmed backdrop. */
static void shade_rect(int x, int y, int w, int h, float f) {
    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= g_rh) continue;
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= g_rw) continue;
            g_framebuf[py * g_rw + px] = col_shade(g_framebuf[py * g_rw + px], f);
        }
    }
}

static void render_inventory(void) {
    const int rw = g_rw, rh = g_rh;
    shade_rect(0, 0, rw, rh, 0.35f);

    int cell = rw / (INV_COLS + 3);
    if (cell < 10) cell = 10;
    if (cell > 34) cell = 34;
    int gap = cell / 8 + 1;
    int gw = INV_COLS * (cell + gap) - gap;
    int gh = INV_ROWS * (cell + gap) - gap;
    int x0 = (rw - gw) / 2, y0 = (rh - gh) / 2 + cell / 2;
    int sc = (rw >= 380) ? 2 : 1;

    fill_rect(x0 - 6, y0 - cell - 6, gw + 12, gh + cell + 12, 0xff1b1b20u);
    fill_rect(x0 - 4, y0 - cell - 4, gw + 8, gh + cell + 8, 0xff2c2c34u);

    int page = g_inv_count ? g_inv_cursor / INV_PAGE : 0;
    int first = page * INV_PAGE;

    char hdr[96];
    snprintf(hdr, sizeof hdr, "%s  [%d]  page %d/%d",
             g_cat_names[g_inv_cat], g_inv_count, page + 1,
             g_inv_count ? (g_inv_count + INV_PAGE - 1) / INV_PAGE : 1);
    font_text(g_framebuf, rw, rh, x0, y0 - cell + 1, hdr, 0xffe8e8f0u, sc);

    char search[40];
    snprintf(search, sizeof search, "find: %s_", g_inv_search);
    font_text(g_framebuf, rw, rh, x0, y0 - cell + 10 * sc, search,
              0xffa8d8ffu, sc);

    for (int i = 0; i < INV_PAGE; i++) {
        int idx = first + i;
        if (idx >= g_inv_count) break;
        int item = g_inv_list[idx];
        int cx = x0 + (i % INV_COLS) * (cell + gap);
        int cy = y0 + (i / INV_COLS) * (cell + gap);
        int sel = (idx == g_inv_cursor);
        fill_rect(cx, cy, cell, cell, sel ? 0xff5a5a70u : 0xff3a3a44u);
        draw_item_ortho(g_framebuf, rw, rh, cx, cy, cell, item, NULL,
                        g_tex_phase, sel ? 1.15f : 1.0f);
    }

    if (g_inv_count) {
        const char *nm = item_name(g_inv_list[g_inv_cursor]);
        int tw = font_width(nm, sc);
        font_text(g_framebuf, rw, rh, (rw - tw) / 2, y0 + gh + 4, nm,
                  0xffffffffu, sc);
    } else {
        font_text(g_framebuf, rw, rh, x0, y0 + 4, "no match", 0xffff8080u, sc);
    }
    font_text(g_framebuf, rw, rh, x0, y0 + gh + 6 + 9 * sc,
              "arrows move  enter take  tab category  esc close",
              0xff9090a0u, 1);
}

/* ----------------------------------------------------------------------- */
/* Overlay: crosshair, held item, hotbar                                    */
/* ----------------------------------------------------------------------- */

/* The pose of whatever is in the player's hand: its looping idle clip, plus
   any one-shot use clip still playing. */
static void held_pose(int item, Pose *out) {
    Pose idle, use;
    anim_pose(item_def(item)->anim_idle, g_time, &idle);
    if (g_use_clip != ANIM_NONE && g_use_time < anim_length(g_use_clip)) {
        anim_pose(g_use_clip, g_use_time, &use);
        anim_blend(&idle, &use, out);
    } else {
        *out = idle;
    }
}

static void render_held_item(void) {
    int item = g_selected;
    if (item <= IT_AIR) return;
    int size = g_rw / 5;
    if (size > ICON_MAX) size = ICON_MAX;
    if (size < 12) return;
    Pose p;
    held_pose(item, &p);
    /* pushed to the lower right, the way a first-person hand sits */
    int x = g_rw - size - g_rw / 24;
    int y = g_rh - size - g_rh / 12;
    float bright = item_light(item) ? 1.3f : 1.05f;
    draw_item_ortho(g_framebuf, g_rw, g_rh, x, y, size, item, &p,
                    g_tex_phase, bright);
}

/* Draws the crosshair, the held item and the hotbar over the finished frame. */
static void render_overlay(void) {
    const int rw = g_rw, rh = g_rh;

    int cxp = rw / 2, cyp = rh / 2;
    for (int i = -5; i <= 5; i++) {
        g_framebuf[cyp * rw + (cxp + i)] ^= 0x00ffffff;
        g_framebuf[(cyp + i) * rw + cxp] ^= 0x00ffffff;
    }

    render_held_item();

    const int gap = 2;
    int sw = 22 * rw / RENDER_W; if (sw < 8) sw = 8;   /* scale with render size */
    int tot = HOTBAR_SLOTS * (sw + gap) - gap;
    int x0 = (rw - tot) / 2, y0 = rh - sw - 6;
    for (int k = 0; k < HOTBAR_SLOTS; k++) {
        int item = g_hotbar[k];
        int sxp = x0 + k * (sw + gap);
        int sel = (k == g_slot);
        fill_rect(sxp - 2, y0 - 2, sw + 4, sw + 4,
                  sel ? 0xffffffffu : 0xff202020u);
        fill_rect(sxp, y0, sw, sw, 0xff3a3a44u);
        if (item > IT_AIR)
            draw_item_ortho(g_framebuf, rw, rh, sxp, y0, sw, item, NULL,
                            g_tex_phase, 1.0f);
    }

    /* name of the held item, above the bar */
    if (g_selected > IT_AIR) {
        int sc = (rw >= 380) ? 2 : 1;
        const char *nm = item_name(g_selected);
        int tw = font_width(nm, sc);
        font_text(g_framebuf, rw, rh, (rw - tw) / 2, y0 - 9 * sc - 2, nm,
                  0xffffffffu, sc);
    }

    if (g_inv_open) render_inventory();
}

/* ----- adaptive resolution ------------------------------------------------
   The renderer is a software raycaster, so cost scales with the pixel count.
   Rather than assume a machine speed, the game measures its own frame time and
   trades internal resolution for frame rate. The image is upscaled to the
   window either way, so a drop costs sharpness, not framing. */
#ifndef HEADLESS_TEST
static const float g_res_scale[] = {1.00f, 0.85f, 0.72f, 0.60f, 0.50f, 0.42f, 0.35f};
#define RES_LEVELS ((int)(sizeof g_res_scale / sizeof g_res_scale[0]))
static int g_res_level = 0;
static int g_autores = 1;

static void set_res_level(int lv) {
    if (lv < 0) lv = 0;
    if (lv >= RES_LEVELS) lv = RES_LEVELS - 1;
    g_res_level = lv;
    int w = (int)(RENDER_W * g_res_scale[lv]) & ~1;
    int h = (int)(RENDER_H * g_res_scale[lv]) & ~1;
    g_rw = w < 64 ? 64 : w;
    g_rh = h < 36 ? 36 : h;
}
#endif /* !HEADLESS_TEST */

/* Fills g_view for the current camera and render size. */
static void view_setup(void) {
    camera_basis(&g_view.fwd, &g_view.right, &g_view.up);
    g_view.ox = g_px; g_view.oy = g_py; g_view.oz = g_pz;
    g_view.tan_v = tanf(FOV_DEG * 0.5f * (float)M_PI / 180.0f);
    float ah = g_view.tan_v * ((float)RENDER_W / RENDER_H);
    g_view.su0 = (2.0f * 0.5f / g_rw - 1.0f) * ah;
    g_view.dsu = (2.0f / g_rw) * ah;

    const int r1 = 120, g1 = 170, b1 = 235;      /* sky top */
    for (int y = 0; y < g_rh; y++) {
        float t = (float)y / g_rh;
        g_view.sky_r[y] = (int)(r1 + (SKY_BOT_R - r1) * t);
        g_view.sky_g[y] = (int)(g1 + (SKY_BOT_G - g1) * t);
        g_view.sky_b[y] = (int)(b1 + (SKY_BOT_B - b1) * t);
    }
}

#ifndef HEADLESS_TEST
/* ----- render worker pool -------------------------------------------------
   Raycasting is embarrassingly parallel: every scanline is independent and
   the only shared state (world, textures, camera) is read-only for the whole
   frame. Worker i renders rows i, i+N, i+2N...; the main thread takes row 0's
   share itself and then waits for the others. Threads are created once and
   parked on an event, so a frame costs two kernel handoffs, not N thread
   creations. */
static HANDLE g_ev_start[MAX_THREADS];
static HANDLE g_ev_done[MAX_THREADS];
static HANDLE g_thread[MAX_THREADS];
static int    g_nthreads = 1;                 /* including the main thread   */
static volatile int g_threads_quit = 0;

static DWORD WINAPI render_worker(LPVOID arg) {
    int id = (int)(intptr_t)arg;
    for (;;) {
        WaitForSingleObject(g_ev_start[id], INFINITE);
        if (g_threads_quit) return 0;
        render_rows(id, g_nthreads);
        SetEvent(g_ev_done[id]);
    }
}

static void render_threads_init(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    if (n < 1) n = 1;
    if (n > MAX_THREADS) n = MAX_THREADS;

    for (int i = 1; i < n; i++) {
        g_ev_start[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_ev_done[i]  = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (g_ev_start[i] && g_ev_done[i])
            g_thread[i] = CreateThread(NULL, 0, render_worker, (LPVOID)(intptr_t)i, 0, NULL);
        if (!g_thread[i]) {                   /* fall back to fewer threads  */
            if (g_ev_start[i]) { CloseHandle(g_ev_start[i]); g_ev_start[i] = NULL; }
            if (g_ev_done[i])  { CloseHandle(g_ev_done[i]);  g_ev_done[i]  = NULL; }
            break;
        }
        g_nthreads = i + 1;                   /* only count threads we got   */
    }
}

static void render_threads_shutdown(void) {
    g_threads_quit = 1;
    for (int i = 1; i < g_nthreads; i++) SetEvent(g_ev_start[i]);
    for (int i = 1; i < g_nthreads; i++) {
        if (g_thread[i]) { WaitForSingleObject(g_thread[i], 1000); CloseHandle(g_thread[i]); }
        if (g_ev_start[i]) CloseHandle(g_ev_start[i]);
        if (g_ev_done[i])  CloseHandle(g_ev_done[i]);
    }
    g_nthreads = 1;
}
#endif /* !HEADLESS_TEST */

static void render_frame(void) {
    view_setup();
#ifndef HEADLESS_TEST
    if (g_nthreads > 1) {
        for (int i = 1; i < g_nthreads; i++) SetEvent(g_ev_start[i]);
        render_rows(0, g_nthreads);
        WaitForMultipleObjects(g_nthreads - 1, &g_ev_done[1], TRUE, INFINITE);
        render_drops();
        render_overlay();
        return;
    }
#endif
    render_rows(0, 1);
    render_drops();
    render_overlay();
}

/* ----------------------------------------------------------------------- */
/* Player physics & interaction                                             */
/* ----------------------------------------------------------------------- */

static const float PLR_RAD = 0.3f;
static const float PLR_HEAD = 0.2f;
static const float PLR_FEET = 1.6f;

/* The player's box against the world. Cells whose model is not a full cube
   are tested box by box, so a slab is half a step up and you can walk through
   a flower. */
static int collide(float ex, float ey, float ez) {
    float bx0 = ex - PLR_RAD, bx1 = ex + PLR_RAD;
    float by0 = ey - PLR_FEET, by1 = ey + PLR_HEAD;
    float bz0 = ez - PLR_RAD, bz1 = ez + PLR_RAD;
    int x0 = (int)floorf(bx0), x1 = (int)floorf(bx1);
    int y0 = (int)floorf(by0), y1 = (int)floorf(by1);
    int z0 = (int)floorf(bz0), z1 = (int)floorf(bz1);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++) {
                int blk = get_block(x, y, z);
                if (!item_is_solid(blk)) continue;
                if (item_is_full_cube(blk)) return 1;
                if (model_overlaps(blk, x, y, z, bx0, by0, bz0, bx1, by1, bz1))
                    return 1;
            }
    return 0;
}

static void update_player(float dt) {
    float cy = cosf(g_yaw), sy = sinf(g_yaw);
    /* forward/right on the horizontal plane */
    float fx = sy, fz = cy;
    float rx = cy, rz = -sy;

    float wish_x = 0, wish_z = 0;
    if (g_keys['W']) { wish_x += fx; wish_z += fz; }
    if (g_keys['S']) { wish_x -= fx; wish_z -= fz; }
    if (g_keys['D']) { wish_x += rx; wish_z += rz; }
    if (g_keys['A']) { wish_x -= rx; wish_z -= rz; }
    float wl = sqrtf(wish_x * wish_x + wish_z * wish_z);
    if (wl > 0) { wish_x /= wl; wish_z /= wl; }

    float speed = g_fly ? 9.0f : 4.5f;

    if (g_fly) {
        g_vx = wish_x * speed; g_vz = wish_z * speed;
        g_vy = 0;
        if (g_keys[0x20])   g_vy = speed;   /* 0x20 = VK_SPACE / ' ' */
        if (g_keys[0x10])   g_vy = -speed;  /* 0x10 = VK_SHIFT       */
    } else {
        g_vx = wish_x * speed; g_vz = wish_z * speed;
        g_vy -= 22.0f * dt;                 /* gravity */
        if (g_keys[0x20] && g_onground) { g_vy = 8.2f; g_onground = 0; }
    }

    /* integrate with per-axis collision */
    float nx = g_px + g_vx * dt;
    if (!collide(nx, g_py, g_pz)) g_px = nx; else g_vx = 0;
    float nz = g_pz + g_vz * dt;
    if (!collide(g_px, g_py, nz)) g_pz = nz; else g_vz = 0;
    float ny = g_py + g_vy * dt;
    if (!collide(g_px, ny, g_pz)) { g_py = ny; g_onground = 0; }
    else { if (g_vy < 0) g_onground = 1; g_vy = 0; }

    if (g_py < -40) { g_py = 60; g_vy = 0; }  /* fell out: reset height */
}

/* Starts the one-shot clip an item plays when it is used. */
static void play_use_anim(int clip) {
    g_use_clip = clip;
    g_use_time = 0.0f;
}

static void do_break(void) {
    V3 f, r, u; camera_basis(&f, &r, &u);
    int hx, hy, hz, nx, ny, nz, blk; float d;
    play_use_anim(ANIM_SWING);
    if (raycast(g_px, g_py, g_pz, f.x, f.y, f.z, REACH,
                &hx, &hy, &hz, &nx, &ny, &nz, &d, &blk)) {
        set_block(hx, hy, hz, IT_AIR);
        drop_spawn(blk, hx + 0.5f, hy + 0.5f, hz + 0.5f);
    }
}

static void do_place(void) {
    V3 f, r, u; camera_basis(&f, &r, &u);
    int hx, hy, hz, nx, ny, nz, blk; float d;
    int item = g_selected;
    /* Non-placeable entries (a sword, a potion, an enchanted book) still play
       their use animation -- they just leave the world alone. */
    play_use_anim(item_def(item)->anim_use);
    if (!item_is_placeable(item)) return;

    if (raycast(g_px, g_py, g_pz, f.x, f.y, f.z, REACH,
                &hx, &hy, &hz, &nx, &ny, &nz, &d, &blk)) {
        int bx = hx + nx, by = hy + ny, bz = hz + nz;
        if (get_block(bx, by, bz) != IT_AIR) return;
        set_block(bx, by, bz, (uint16_t)item);
        /* A model with no collision (a torch, a flower) can share a cell with
           the player; anything solid must not be placed inside them. */
        if (collide(g_px, g_py, g_pz)) set_block(bx, by, bz, IT_AIR);
    }
}

/* ----------------------------------------------------------------------- */
/* Win32 plumbing                                                           */
/* ----------------------------------------------------------------------- */
#ifndef HEADLESS_TEST

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: g_running = 0; PostQuitMessage(0); return 0;
    case WM_SIZE:    g_client_w = LOWORD(l); g_client_h = HIWORD(l); return 0;
    case WM_SETFOCUS:  g_focused = 1; return 0;
    case WM_KILLFOCUS: g_focused = 0; return 0;
    case WM_KEYDOWN:
        if (w < 256) g_keys[w] = 1;
        if (w == VK_ESCAPE) { g_running = 0; PostQuitMessage(0); }
        if (w == 'F') g_fly = !g_fly;
        if (w == 'R') gen_world((unsigned)GetTickCount());
        if (w == 'K') save_world("world.sav");
        if (w == 'L') load_world("world.sav");
        if (w >= '1' && w <= '9') {
            int idx = (int)w - '0';          /* 1..9 -> block ids 1..9 */
            if (idx < B_COUNT) g_selected = idx;
        }
        if (w == '0') g_selected = B_GLASS;
        if (w == 'T') g_autores = !g_autores;
        if (w == VK_OEM_MINUS || w == VK_SUBTRACT) { g_autores = 0; set_res_level(g_res_level + 1); }
        if (w == VK_OEM_PLUS  || w == VK_ADD)      { g_autores = 0; set_res_level(g_res_level - 1); }
        return 0;
    case WM_KEYUP:
        if (w < 256) g_keys[w] = 0;
        return 0;
    case WM_LBUTTONDOWN: g_break_req = 1; return 0;
    case WM_RBUTTONDOWN: g_place_req = 1; return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void center_mouse(int *cx, int *cy) {
    RECT rc; GetClientRect(g_hwnd, &rc);
    POINT p; p.x = (rc.right - rc.left) / 2; p.y = (rc.bottom - rc.top) / 2;
    *cx = p.x; *cy = p.y;
    ClientToScreen(g_hwnd, &p);
    SetCursorPos(p.x, p.y);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int nShow) {
    (void)hPrev; (void)cmd;

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MiniCraftWnd";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    RECT r = {0, 0, WIN_W, WIN_H};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindow("MiniCraftWnd", "MiniCraft - voxel sandbox (32/64-bit)",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          r.right - r.left, r.bottom - r.top,
                          NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;
    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    gen_textures();
    load_assets();
    render_threads_init();
    gen_world((unsigned)GetTickCount());
    load_world("world.sav");   /* resume a saved world if one exists */

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    int mouse_init = 0, cursor_hidden = 0;
    float ft_avg = 1.0f / 60.0f;      /* smoothed frame time, seconds */
    float since_res = 0, since_title = 1e9f;
    HDC hdc = GetDC(g_hwnd);
    SetStretchBltMode(hdc, COLORONCOLOR);   /* cheapest upscale filter */

    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_running) break;

        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        /* mouse look */
        if (g_focused) {
            int cx, cy; RECT rc; GetClientRect(g_hwnd, &rc);
            cx = (rc.right - rc.left) / 2; cy = (rc.bottom - rc.top) / 2;
            POINT p; GetCursorPos(&p); ScreenToClient(g_hwnd, &p);
            if (mouse_init) {
                float mdx = (float)(p.x - cx), mdy = (float)(p.y - cy);
                g_yaw   -= mdx * 0.0025f;
                g_pitch -= mdy * 0.0025f;
                float lim = 1.55f;
                if (g_pitch > lim) g_pitch = lim;
                if (g_pitch < -lim) g_pitch = -lim;
            }
            int scx, scy; center_mouse(&scx, &scy);
            mouse_init = 1;
            /* ShowCursor keeps a counter, so this must be edge-triggered --
               calling it every frame drove the counter to -infinity and left
               the cursor stuck hidden after alt-tabbing away. */
            if (!cursor_hidden) { ShowCursor(FALSE); cursor_hidden = 1; }
        } else {
            mouse_init = 0;
            if (cursor_hidden) { ShowCursor(TRUE); cursor_hidden = 0; }
            Sleep(20);          /* don't burn a core in the background */
        }

        if (g_break_req) { do_break(); g_break_req = 0; }
        if (g_place_req) { do_place(); g_place_req = 0; }

        update_player(dt);

        /* --- adaptive resolution ---------------------------------------- */
        ft_avg += (dt - ft_avg) * 0.1f;
        since_res += dt;
        /* Only judge speed while focused -- a backgrounded frame is padded
           with Sleep() and would drag the resolution down for no reason. */
        if (g_autores && g_focused && since_res > 0.4f) {
            if (ft_avg > 1.0f / 45.0f && g_res_level < RES_LEVELS - 1) {
                set_res_level(g_res_level + 1); since_res = 0;
            } else if (ft_avg < 1.0f / 110.0f && g_res_level > 0) {
                set_res_level(g_res_level - 1); since_res = 0;
            }
        }

        render_frame();

        bmi.bmiHeader.biWidth  =  g_rw;
        bmi.bmiHeader.biHeight = -g_rh;          /* top-down */
        StretchDIBits(hdc, 0, 0, g_client_w, g_client_h,
                      0, 0, g_rw, g_rh,
                      g_framebuf, &bmi, DIB_RGB_COLORS, SRCCOPY);

        since_title += dt;
        if (since_title > 0.5f) {
            char title[192];
            snprintf(title, sizeof title,
                     "MiniCraft - voxel sandbox  |  %.0f FPS  |  %dx%d%s  |  "
                     "%d thread%s  |  %d textures",
                     ft_avg > 0 ? 1.0f / ft_avg : 0.0f, g_rw, g_rh,
                     g_autores ? "" : " (locked)",
                     g_nthreads, g_nthreads == 1 ? "" : "s", g_assets_loaded);
            SetWindowTextA(g_hwnd, title);
            since_title = 0;
        }
    }

    render_threads_shutdown();
    ReleaseDC(g_hwnd, hdc);
    return 0;
}

#endif /* !HEADLESS_TEST */
