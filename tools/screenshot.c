/* Renders the game headlessly to PNG screenshots (no Windows needed).
 * Reuses the exact in-game renderer (render_frame -> g_framebuf). */
#define HEADLESS_TEST
#define PNG_ENABLE_WRITE
#include "../src/main.c"

static void save_fb(const char *name, int scale) {
    int W = RENDER_W * scale, H = RENDER_H * scale;
    uint32_t *big = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            big[y * W + x] = g_framebuf[(y / scale) * RENDER_W + (x / scale)] | 0xff000000u;
    char path[256];
    snprintf(path, sizeof path, "screenshots/%s.png", name);
    png_write(path, big, W, H);
    free(big);
    printf("  %s (%dx%d)\n", path, W, H);
}
static void save_shot(const char *name, int scale) { render_frame(); save_fb(name, scale); }

int main(void) {
    system("mkdir -p screenshots");
    gen_textures();
    load_assets();
    gen_world(2024);

    /* remember spawn */
    float sx = g_px, sy = g_py, sz = g_pz;

    /* 1: elevated look across the terrain */
    g_px = sx; g_py = sy + 8; g_pz = sz; g_yaw = 0.7f; g_pitch = -0.32f;
    save_shot("01_spawn_view", 2);

    /* 2: another direction */
    g_px = sx; g_py = sy + 10; g_pz = sz; g_yaw = 2.6f; g_pitch = -0.35f;
    save_shot("02_landscape", 2);

    /* 3: high vista */
    g_px = sx; g_pz = sz; g_py = sy + 16; g_yaw = 3.9f; g_pitch = -0.5f;
    save_shot("03_vista", 2);

    /* 4: a little house, viewed from outside */
    gen_world(7);
    sx = g_px; sy = g_py; sz = g_pz;
    int bx = (int)sx + 5, by = (int)sy - 1, bz = (int)sz;
    for (int dx = 0; dx < 5; dx++)          /* floor */
        for (int dz = 0; dz < 5; dz++) set_block(bx + dx, by, bz + dz, B_PLANKS);
    for (int dy = 1; dy <= 3; dy++)          /* walls */
        for (int dx = 0; dx < 5; dx++)
            for (int dz = 0; dz < 5; dz++)
                if (dx == 0 || dx == 4 || dz == 0 || dz == 4) {
                    int t = (dy == 2 && (dx == 2 || dz == 2)) ? B_GLASS : B_COBBLE;
                    set_block(bx + dx, by + dy, bz + dz, t);
                }
    for (int dx = 0; dx < 5; dx++)           /* roof */
        for (int dz = 0; dz < 5; dz++) set_block(bx + dx, by + 4, bz + dz, B_LOG);
    set_block(bx + 2, by + 1, bz, B_AIR); set_block(bx + 2, by + 2, bz, B_AIR); /* door */
    g_px = sx - 2; g_py = sy + 4; g_pz = sz - 3; g_yaw = 0.9f; g_pitch = -0.35f;
    save_shot("04_house", 2);

    /* ---------------- UI screens ---------------- */
    gen_world(2024);
    sx = g_px; sy = g_py; sz = g_pz;
    g_px = sx; g_py = sy + 10; g_pz = sz;    /* elevated panorama camera */

    /* main menu */
    g_state = ST_MENU; g_menu_sel = 0; g_menu_yaw = 0.6f;
    render_menu();
    save_fb("05_main_menu", 2);

    /* create world (seed typed in) */
    g_state = ST_CREATE; g_menu_sel = 0;
    snprintf(g_input, sizeof g_input, "minecraft"); g_input_len = 9;
    render_frame_menu_bg();
    render_create();
    save_fb("06_create_world", 2);

    /* settings */
    g_state = ST_SETTINGS; g_menu_sel = 1;
    render_frame_menu_bg();
    render_settings();
    save_fb("07_settings", 2);

    /* in-game console with command output */
    g_state = ST_PLAY; g_console_open = 0; g_inv_open = 0;
    g_daylight = 1.0f;
    g_px = sx; g_py = sy; g_pz = sz; g_yaw = 0.7f; g_pitch = -0.15f;
    exec_command("help");
    exec_command("seed");
    exec_command("time night");
    exec_command("give glass 12");
    g_console_open = 1;
    snprintf(g_input, sizeof g_input, "tp 64 40 64"); g_input_len = 11;
    render_frame();
    render_console_overlay();
    save_fb("08_console", 2);

    /* survival HUD: hearts + hotbar with collected blocks */
    g_console_open = 0; g_daylight = 1.0f;
    set_gamemode(0);
    inv_give(B_GRASS, 34); inv_give(B_DIRT, 12); inv_give(B_STONE, 48);
    inv_give(B_LOG, 7); inv_give(B_COBBLE, 21); inv_give(B_SAND, 5);
    g_health = 14; g_hotbar_sel = 2;
    g_px = sx; g_py = sy; g_pz = sz; g_yaw = 0.7f; g_pitch = -0.12f;
    render_frame(); draw_hud();
    save_fb("09_survival_hud", 2);

    /* survival inventory screen */
    inv_give(B_PLANKS, 30); inv_give(B_LEAVES, 16); inv_give(B_GLASS, 9);
    g_inv_open = 1; g_mouse_ix = RENDER_W / 2 + 40; g_mouse_iy = 120;
    g_hand.block = B_STONE; g_hand.count = 12;
    render_frame(); render_inventory();
    save_fb("10_inventory", 2);

    /* creative inventory */
    set_gamemode(1); g_inv_open = 1; g_hand.block = 0; g_hand.count = 0;
    render_frame(); render_inventory();
    save_fb("11_creative_inventory", 2);

    /* day/night cycle: sunrise, noon, sunset, night (sun & moon) */
    gen_world(2024);
    sx = g_px; sy = g_py; sz = g_pz;
    g_inv_open = 0; g_console_open = 0; g_gamemode = 1;
    struct { const char *name; float tod; float yaw; float pitch; } times[] = {
        {"12_sunrise", 0.25f, 1.57f, 0.10f},
        {"13_noon",    0.50f, 0.70f, 0.30f},
        {"14_sunset",  0.75f, 4.71f, 0.10f},
        {"15_night",   0.02f, 3.48f, 1.05f},
    };
    for (int i = 0; i < 4; i++) {
        g_tod = times[i].tod; g_daylight = daylight_from_tod(g_tod);
        g_px = sx; g_py = sy + 4; g_pz = sz; g_yaw = times[i].yaw; g_pitch = times[i].pitch;
        render_frame(); draw_hud();
        save_fb(times[i].name, 2);
    }

    printf("done.\n");
    return 0;
}
