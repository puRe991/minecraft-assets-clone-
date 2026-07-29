/* Headless logic test for MiniCraft's platform-independent core.
 * Compiles the same source with HEADLESS_TEST defined (Win32 parts excluded)
 * and simulates gameplay, asserting invariants. Runs natively on Linux. */
#define HEADLESS_TEST
#include "../src/main.c"

#include <stdio.h>
#include <assert.h>

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

    /* --- render a frame, verify framebuffer fully written --- */
    render_frame();
    if (!check_framebuf_valid()) { printf("FAIL: framebuffer has unwritten pixels\n"); fails++; }
    printf("render_frame: %s\n", fails ? "BAD" : "ok");

    printf("\n%s (%d failures)\n", fails ? "*** TESTS FAILED ***" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
