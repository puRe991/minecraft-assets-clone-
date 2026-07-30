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

    /* --- inventory helpers --- */
    set_gamemode(0);                       /* survival: empty inventory */
    inv_give(B_STONE, 5);
    inv_give(B_STONE, 3);                  /* should merge into one stack */
    if (g_inv[0].block != B_STONE || g_inv[0].count != 8) { printf("FAIL: inv_give merge\n"); fails++; }
    g_hotbar_sel = 0;
    for (int i = 0; i < 8; i++) consume_selected();
    if (g_inv[0].count != 0 || g_inv[0].block != 0) { printf("FAIL: consume_selected\n"); fails++; }
    /* inv_click: pick up and move a stack */
    inv_give(B_DIRT, 10);
    inv_click(0, 0);                       /* pick up from slot 0 */
    if (g_hand.count != 10 || g_inv[0].count != 0) { printf("FAIL: inv_click pickup\n"); fails++; }
    inv_click(5, 0);                       /* drop into slot 5 */
    if (g_inv[5].count != 10 || g_hand.count != 0) { printf("FAIL: inv_click drop\n"); fails++; }
    printf("inventory: %s\n", fails ? "BAD" : "ok");

    /* --- survival break collects, place consumes --- */
    gen_world(55); set_gamemode(0);
    g_yaw = 0; g_pitch = -1.5f;            /* look straight down at the ground */
    do_break();
    int got = 0; for (int i = 0; i < INV_SLOTS; i++) got += (g_inv[i].count > 0 ? g_inv[i].count : 0);
    if (got < 1) { printf("FAIL: survival break gave no item\n"); fails++; }
    /* select the collected block and place it back */
    for (int i = 0; i < INV_COLS; i++) if (g_inv[i].count > 0) { g_hotbar_sel = i; break; }
    int before = g_inv[g_hotbar_sel].count;
    g_pitch = -1.5f; do_place();
    if (g_inv[g_hotbar_sel].count > before) { printf("FAIL: place did not consume\n"); fails++; }
    printf("survival break/place: %s\n", fails ? "BAD" : "ok");

    /* --- creative infinite blocks are not consumed --- */
    set_gamemode(1);
    g_hotbar_sel = 2; int cbefore = g_inv[2].count;   /* -1 (infinite) */
    g_pitch = -1.5f; do_place();
    if (g_inv[2].count != cbefore) { printf("FAIL: creative consumed a block\n"); fails++; }
    printf("creative infinite: %s\n", fails ? "BAD" : "ok");

    /* --- fall damage (survival) --- */
    gen_world(77); set_gamemode(0);
    int gy = WORLD_Y - 1; while (gy > 0 && get_block(8, gy, 8) == B_AIR) gy--;
    g_px = 8.5f; g_pz = 8.5f; g_py = gy + 10.0f;    /* ~7-block fall: hurts, not lethal */
    g_vy = 0; g_air_max_y = g_py; g_was_ground = 0; g_health = 20;
    for (int s = 0; s < 400; s++) { update_player(1.0f / 60.0f); if (g_was_ground && g_onground) break; }
    if (g_health >= 20) { printf("FAIL: no fall damage after a drop\n"); fails++; }
    if (g_health <= 0)  { printf("FAIL: non-lethal drop killed the player\n"); fails++; }
    printf("fall damage (hp=%d after ~7-block drop): %s\n", g_health, fails ? "BAD" : "ok");

    /* --- audio synthesis: bounded, non-silent, reacts to events --- */
    {
        static int16_t abuf[1024 * 2];
        audio_set_biome(BIO_FOREST, 0);
        long nonzero = 0; int inrange = 1;
        for (int block = 0; block < 60; block++) {          /* ~3 s of audio */
            if (block == 20) audio_sfx(2);                  /* a break sound */
            if (block == 40) audio_set_biome(BIO_DESERT, 0);
            audio_synth(abuf, 1024);
            for (int i = 0; i < 1024 * 2; i++) {
                if (abuf[i] != 0) nonzero++;
                /* int16 is inherently in range; assert the synth didn't wrap oddly */
                if (abuf[i] == -32768) inrange = 0;
            }
        }
        if (!inrange) { printf("FAIL: audio sample out of range\n"); fails++; }
        if (nonzero < 1000) { printf("FAIL: audio essentially silent (%ld)\n", nonzero); fails++; }
        printf("audio synthesis (bounded, non-silent): %s\n", fails ? "BAD" : "ok");
    }

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
