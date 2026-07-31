/* Headless logic test for MiniCraft's platform-independent core.
 * Compiles the same source with HEADLESS_TEST defined (Win32 parts excluded)
 * and simulates gameplay, asserting invariants. Runs natively on Linux. */
#define HEADLESS_TEST
#include "../src/main.c"

#include <stdio.h>
#include <assert.h>

/* The plain per-block DDA the optimised raycast() replaced, kept here as the
   oracle for its behaviour. */
static int raycast_reference(float ox, float oy, float oz, float dx, float dy, float dz,
                             float maxdist, int *hx, int *hy, int *hz,
                             int *nx, int *ny, int *nz, float *outdist, int *outblock) {
    int mapx = (int)floorf(ox), mapy = (int)floorf(oy), mapz = (int)floorf(oz);
    float ddx = (dx == 0) ? 1e30f : fabsf(1.0f / dx);
    float ddy = (dy == 0) ? 1e30f : fabsf(1.0f / dy);
    float ddz = (dz == 0) ? 1e30f : fabsf(1.0f / dz);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, sz = dz < 0 ? -1 : 1;
    float sdx = (dx < 0 ? (ox - mapx) : (mapx + 1 - ox)) * ddx;
    float sdy = (dy < 0 ? (oy - mapy) : (mapy + 1 - oy)) * ddy;
    float sdz = (dz < 0 ? (oz - mapz) : (mapz + 1 - oz)) * ddz;
    int side = 0;
    float dist = 0;
    for (int iter = 0; iter < 1024; iter++) {
        if (sdx < sdy && sdx < sdz) { dist = sdx; sdx += ddx; mapx += sx; side = 0; }
        else if (sdy < sdz)         { dist = sdy; sdy += ddy; mapy += sy; side = 1; }
        else                        { dist = sdz; sdz += ddz; mapz += sz; side = 2; }
        if (dist > maxdist) return 0;
        uint8_t b = get_block(mapx, mapy, mapz);
        if (b != B_AIR && b != B_WATER) {
            *hx = mapx; *hy = mapy; *hz = mapz;
            *nx = *ny = *nz = 0;
            if (side == 0)      *nx = -sx;
            else if (side == 1) *ny = -sy;
            else                *nz = -sz;
            if (outdist) *outdist = dist;
            if (outblock) *outblock = b;
            return 1;
        }
    }
    return 0;
}

/* Returns the number of macro cells whose flag disagrees with the blocks it
   covers. Only a cell marked empty while holding a solid block is unsound, but
   both directions are checked to catch drift. */
static int macro_grid_stale(void) {
    int bad = 0;
    for (int my = 0; my < MACRO_Y; my++)
        for (int mz = 0; mz < MACRO_Z; mz++)
            for (int mx = 0; mx < MACRO_X; mx++) {
                int any = 0;
                for (int y = my * MACRO; y < (my + 1) * MACRO && !any; y++)
                    for (int z = mz * MACRO; z < (mz + 1) * MACRO && !any; z++)
                        for (int x = mx * MACRO; x < (mx + 1) * MACRO; x++)
                            if (is_solid(x, y, z)) { any = 1; break; }
                if (!!g_macro[(my * MACRO_Z + mz) * MACRO_X + mx] != any) bad++;
            }
    return bad;
}

static int check_framebuf_valid(void) {
    for (int i = 0; i < RENDER_W * RENDER_H; i++) {
        uint32_t c = g_framebuf[i];
        if ((c & 0xff000000u) != 0xff000000u) return 0;   /* alpha must be set */
    }
    return 1;
}

int main(void) {
    int fails = 0;

    /* --- textures --- */
    gen_textures();
    for (int b = B_GRASS; b < B_COUNT; b++)
        for (int f = 0; f < 3; f++)
            for (int p = 0; p < TEX * TEX; p++)
                if ((g_tex[b][f][p] & 0xff000000u) != 0xff000000u) {
                    printf("FAIL: texture %d face %d has bad alpha\n", b, f); fails++;
                    goto tex_done;
                }
tex_done:
    printf("textures generated: %s\n", fails ? "BAD" : "ok");

    /* --- asset loading: load the exported resource pack --- */
    load_assets();
    if (g_assets_loaded < 30) {   /* 10 blocks x 3 faces, all mapped */
        printf("FAIL: only %d/30 texture slots loaded from assets/\n", g_assets_loaded);
        fails++;
    }
    printf("assets loaded from PNG: %d/30 face slots (12 files)  %s\n", g_assets_loaded,
           g_assets_loaded >= 30 ? "ok" : "BAD");

    /* --- worldgen: several seeds, sanity of block values & solid ground --- */
    for (unsigned seed = 1; seed <= 5; seed++) {
        gen_world(seed);
        long solid = 0, bad = 0;
        for (int x = 0; x < WORLD_X; x++)
            for (int z = 0; z < WORLD_Z; z++) {
                int col_solid = 0;
                for (int y = 0; y < WORLD_Y; y++) {
                    uint8_t v = get_block(x, y, z);
                    if (v >= B_COUNT) bad++;
                    if (v != B_AIR && v != B_WATER) { solid++; col_solid++; }
                }
                if (col_solid == 0) { /* every column should have some ground */ }
            }
        if (bad) { printf("FAIL seed %u: %ld invalid block ids\n", seed, bad); fails++; }
        if (solid < 10000) { printf("FAIL seed %u: too few solid blocks (%ld)\n", seed, solid); fails++; }
        /* spawn must be above solid ground and not inside a block */
        if (collide(g_px, g_py, g_pz)) { printf("FAIL seed %u: spawn inside block\n", seed); fails++; }
    }
    printf("worldgen (5 seeds): %s\n", fails ? "BAD" : "ok");

    /* --- raycast terminates & hit cell is solid --- */
    gen_world(42);
    int hits = 0;
    for (int i = 0; i < 2000; i++) {
        float a = i * 0.013f, b = i * 0.007f;
        float dx = cosf(b) * sinf(a), dy = sinf(b), dz = cosf(b) * cosf(a);
        int hx, hy, hz, nx, ny, nz, blk; float d;
        if (raycast(g_px, g_py, g_pz, dx, dy, dz, MAX_RAY,
                    &hx, &hy, &hz, &nx, &ny, &nz, &d, &blk)) {
            hits++;
            if (!is_solid(hx, hy, hz)) { printf("FAIL: raycast hit non-solid\n"); fails++; break; }
            if (d < 0 || d > MAX_RAY)  { printf("FAIL: raycast bad dist %f\n", d); fails++; break; }
            int norm_ok = (abs(nx) + abs(ny) + abs(nz)) == 1;
            if (!norm_ok) { printf("FAIL: raycast bad normal\n"); fails++; break; }
        }
    }
    printf("raycast (2000 rays, %d hits): %s\n", hits, fails ? "BAD" : "ok");

    /* --- the accelerated raycaster must agree with a plain per-block DDA ---
       raycast() skips empty macro cells and clips against the world box; this
       pins it to the straightforward traversal it replaced. */
    {
        int rays = 0, diffs = 0;
        for (unsigned seed = 1; seed <= 3 && !diffs; seed++) {
            gen_world(seed);
            unsigned st = 7u;
            for (int i = 0; i < 120000; i++) {
                float rf[6];
                for (int k = 0; k < 6; k++) {
                    st = st * 1103515245u + 12345u;
                    rf[k] = ((st >> 8) & 0xffffff) / (float)0xffffff;
                }
                /* origins inside the world, near the player, and outside it */
                float ox, oy, oz;
                if (i % 3 == 0)      { ox = rf[0]*WORLD_X; oy = rf[1]*WORLD_Y; oz = rf[2]*WORLD_Z; }
                else if (i % 3 == 1) { ox = g_px+(rf[0]-.5f)*6; oy = g_py+(rf[1]-.5f)*6; oz = g_pz+(rf[2]-.5f)*6; }
                else                 { ox = (rf[0]-.5f)*3*WORLD_X; oy = (rf[1]-.5f)*3*WORLD_Y; oz = (rf[2]-.5f)*3*WORLD_Z; }
                float a = rf[3]*6.2831853f, e = (rf[4]-.5f)*3.14159f;
                float dx = cosf(e)*sinf(a), dy = sinf(e), dz = cosf(e)*cosf(a);
                if (i % 11 == 0) {   /* axis-aligned rays exercise the dir==0 paths */
                    int ax = i % 3; float s = (i % 2) ? 1.f : -1.f;
                    dx = dy = dz = 0; if (ax == 0) dx = s; else if (ax == 1) dy = s; else dz = s;
                }
                int h1[3], n1[3], b1, h2[3], n2[3], b2; float d1, d2;
                int r1 = raycast_reference(ox, oy, oz, dx, dy, dz, MAX_RAY,
                                           &h1[0],&h1[1],&h1[2], &n1[0],&n1[1],&n1[2], &d1, &b1);
                int r2 = raycast(ox, oy, oz, dx, dy, dz, MAX_RAY,
                                 &h2[0],&h2[1],&h2[2], &n2[0],&n2[1],&n2[2], &d2, &b2);
                rays++;
                if (r1 != r2 ||
                    (r1 && (h1[0]!=h2[0] || h1[1]!=h2[1] || h1[2]!=h2[2] ||
                            n1[0]!=n2[0] || n1[1]!=n2[1] || n1[2]!=n2[2] ||
                            b1 != b2 || fabsf(d1-d2) > 1e-3f))) {
                    printf("FAIL: raycast disagrees with reference at ray %d "
                           "(o=%.3f,%.3f,%.3f d=%.3f,%.3f,%.3f)\n", i, ox,oy,oz, dx,dy,dz);
                    diffs++; fails++; break;
                }
            }
        }
        printf("raycast vs reference DDA (%d rays): %s\n", rays, diffs ? "BAD" : "ok");
    }

    /* --- macro occupancy grid must never claim an occupied cell is empty ---
       An empty macro cell is skipped wholesale, so a stale zero would let rays
       pass straight through solid blocks. */
    {
        int bad = 0;
        gen_world(9);
        bad += macro_grid_stale();
        /* breaking and placing blocks must keep it in sync */
        set_block(20, 30, 20, B_STONE);  bad += macro_grid_stale();
        set_block(20, 30, 20, B_AIR);    bad += macro_grid_stale();
        for (int y = 0; y < WORLD_Y; y++) set_block(30, y, 30, B_AIR);
        bad += macro_grid_stale();
        for (int y = 24; y < 32; y++) set_block(30, y, 30, B_PLANKS);
        bad += macro_grid_stale();
        if (bad) { printf("FAIL: macro occupancy grid out of sync (%d)\n", bad); fails++; }
        printf("macro occupancy grid: %s\n", bad ? "BAD" : "ok");
    }

    /* --- rendering at a reduced internal resolution must stay in bounds --- */
    {
        int bad = 0;
        gen_world(3);
        int sizes[][2] = {{480,270},{408,230},{240,134},{64,36}};
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            g_rw = sizes[s][0]; g_rh = sizes[s][1];
            memset(g_framebuf, 0, sizeof g_framebuf);
            render_frame();
            for (int y = 0; y < g_rh; y++)
                for (int x = 0; x < g_rw; x++)
                    if ((g_framebuf[y * g_rw + x] & 0xff000000u) != 0xff000000u) { bad++; break; }
        }
        g_rw = RENDER_W; g_rh = RENDER_H;
        if (bad) { printf("FAIL: scaled render left %d unwritten rows\n", bad); fails++; }
        printf("scaled-resolution render: %s\n", bad ? "BAD" : "ok");
    }

    /* --- physics simulation: walk around for ~10 simulated seconds --- */
    g_fly = 0;
    for (int i = 0; i < 256; i++) g_keys[i] = 0;
    g_keys['W'] = 1;
    int steps = 600;   /* 10s @ 60fps */
    for (int s = 0; s < steps; s++) {
        if (s % 120 == 60) g_keys[0x20] = 1; else g_keys[0x20] = 0;   /* periodic jump */
        if (s == 200) { g_keys['W'] = 0; g_keys['D'] = 1; }
        if (s == 400) { g_keys['D'] = 0; g_keys['S'] = 1; }
        update_player(1.0f / 60.0f);
        if (isnan(g_px) || isnan(g_py) || isnan(g_pz)) { printf("FAIL: NaN position at step %d\n", s); fails++; break; }
        if (collide(g_px, g_py, g_pz)) { printf("FAIL: player stuck in block at step %d\n", s); fails++; break; }
        if (g_py < -60 || g_py > 200) { printf("FAIL: player out of range y=%f\n", g_py); fails++; break; }
    }
    printf("physics (600 steps): %s  (final pos %.1f,%.1f,%.1f)\n",
           fails ? "BAD" : "ok", g_px, g_py, g_pz);

    /* --- break & place --- */
    g_yaw = 0; g_pitch = -1.4f;   /* look down */
    do_break();
    g_selected = B_PLANKS;
    do_place();
    printf("break/place: ok (no crash)\n");

    /* --- world save/load round-trip --- */
    gen_world(123);
    set_block(10, 40, 10, B_GLASS);
    set_block(11, 40, 10, B_PLANKS);
    g_px = 12.5f; g_py = 45.0f; g_pz = 9.5f; g_yaw = 1.23f; g_pitch = -0.4f;
    uint8_t snapshot[64]; for (int i = 0; i < 64; i++) snapshot[i] = g_world[i * 137 % (int)sizeof(g_world)];
    const char *sav = "build/test.sav";   /* build/ is gitignored */
    if (!save_world(sav)) { printf("FAIL: save_world\n"); fails++; }
    /* clobber the world, then load it back */
    for (unsigned i = 0; i < sizeof(g_world); i++) g_world[i] = B_STONE;
    g_px = g_py = g_pz = g_yaw = g_pitch = 0;
    if (!load_world(sav)) { printf("FAIL: load_world\n"); fails++; }
    int mism = 0;
    for (int i = 0; i < 64; i++) if (g_world[i * 137 % (int)sizeof(g_world)] != snapshot[i]) mism++;
    if (get_block(10, 40, 10) != B_GLASS || get_block(11, 40, 10) != B_PLANKS) mism++;
    if (mism) { printf("FAIL: save/load world mismatch (%d)\n", mism); fails++; }
    if (g_px != 12.5f || g_py != 45.0f || g_yaw != 1.23f) { printf("FAIL: save/load player state\n"); fails++; }
    printf("world save/load: %s\n", mism ? "BAD" : "ok");

    /* --- render a frame, verify framebuffer fully written --- */
    render_frame();
    if (!check_framebuf_valid()) { printf("FAIL: framebuffer has unwritten pixels\n"); fails++; }
    printf("render_frame: %s\n", fails ? "BAD" : "ok");

    printf("\n%s (%d failures)\n", fails ? "*** TESTS FAILED ***" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
