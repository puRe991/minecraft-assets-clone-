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

    /* --- worldgen: several seeds, sanity over the loaded chunk area --- */
    for (unsigned seed = 1; seed <= 5; seed++) {
        gen_world(seed);
        long solid = 0, bad = 0;
        int span = LOADR * CH;              /* sample the loaded region */
        for (int x = -span; x < span; x += 2)
            for (int z = -span; z < span; z += 2)
                for (int y = 0; y < WORLD_Y; y++) {
                    uint8_t v = get_block(x, y, z);
                    if (v >= B_COUNT) bad++;
                    if (v != B_AIR && v != B_WATER) solid++;
                }
        if (bad) { printf("FAIL seed %u: %ld invalid block ids\n", seed, bad); fails++; }
        if (solid < 10000) { printf("FAIL seed %u: too few solid blocks (%ld)\n", seed, solid); fails++; }
        if (collide(g_px, g_py, g_pz)) { printf("FAIL seed %u: spawn inside block\n", seed); fails++; }
    }
    printf("worldgen (5 seeds): %s\n", fails ? "BAD" : "ok");

    /* --- chunk streaming: walking far loads new chunks under the player --- */
    gen_world(99);
    for (int step = 0; step < 400; step++) {
        g_px += CH * 0.5f;                  /* walk +X across many chunks */
        stream_chunks();
        int gx = (int)floorf(g_px);
        int ground = WORLD_Y - 1;
        while (ground > 0 && get_block(gx, ground, (int)floorf(g_pz)) == B_AIR) ground--;
        if (ground <= 0) { printf("FAIL: no ground after streaming at x=%d\n", gx); fails++; break; }
    }
    printf("chunk streaming (walked ~%d blocks): %s\n", (int)(400 * CH * 0.5f), fails ? "BAD" : "ok");

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

    /* --- world save/load round-trip --- */
    gen_world(123);
    set_block(10, 40, 10, B_GLASS);
    set_block(11, 40, 10, B_PLANKS);
    set_block(-5, 38, 3, B_COBBLE);          /* negative coords (another chunk) */
    g_px = 12.5f; g_py = 45.0f; g_pz = 9.5f; g_yaw = 1.23f; g_pitch = -0.4f;
    const char *sav = "/tmp/claude-0/-home-user-minecraft-assets-clone-/3dbf0045-846a-5245-96cd-022269b286fa/scratchpad/test.sav";
    if (!save_world(sav)) { printf("FAIL: save_world\n"); fails++; }
    /* wipe all chunks, then load them back */
    for (int i = 0; i < GS * GS; i++) g_chunks[i].valid = 0;
    g_px = g_py = g_pz = g_yaw = g_pitch = 0;
    if (!load_world(sav)) { printf("FAIL: load_world\n"); fails++; }
    int mism = 0;
    if (get_block(10, 40, 10) != B_GLASS) mism++;
    if (get_block(11, 40, 10) != B_PLANKS) mism++;
    if (get_block(-5, 38, 3) != B_COBBLE) mism++;
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
