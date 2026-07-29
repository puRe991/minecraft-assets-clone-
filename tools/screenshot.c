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
    g_state = ST_PLAY; g_draw_hud = 1;
    g_px = sx; g_py = sy; g_pz = sz; g_yaw = 0.7f; g_pitch = -0.15f;
    exec_command("help");
    exec_command("seed");
    exec_command("time night");
    exec_command("give glass");
    g_console_open = 1;
    snprintf(g_input, sizeof g_input, "tp 64 40 64"); g_input_len = 11;
    render_frame();
    render_console_overlay();
    save_fb("08_console", 2);

    printf("done.\n");
    return 0;
}
