/* Exports MiniCraft's procedural textures to a Minecraft-format resource
 * pack: assets/minecraft/textures/block/<name>.png (16x16 RGBA PNGs).
 *
 * Textures that MiniCraft tints at load time (grass, leaves) are written as
 * grayscale, matching how Minecraft ships biome-tinted textures - so both
 * this sample pack and a stock Minecraft pack render correctly.
 *
 * Runs natively on Linux at build time. Reuses the game's texture generator. */
#define HEADLESS_TEST
#define PNG_ENABLE_WRITE
#include "../src/main.c"

#include <sys/stat.h>
#include <sys/types.h>

static void mkdirs(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static uint32_t to_gray(uint32_t c) {
    int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    int y = (r * 77 + g * 150 + b * 29) >> 8;
    y = y * 160 / 100;                 /* brighten so tinting isn't too dark */
    if (y > 255) y = 255;
    return 0xff000000u | (y << 16) | (y << 8) | y;
}

static void write_tex(const char *dir, const char *name, const uint32_t *src, int gray) {
    uint32_t buf[TEX * TEX];
    for (int i = 0; i < TEX * TEX; i++) buf[i] = gray ? to_gray(src[i]) : (src[i] | 0xff000000u);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    if (png_write(path, buf, TEX, TEX) == 0) printf("  wrote %s.png%s\n", name, gray ? " (grayscale)" : "");
    else printf("  FAILED %s.png\n", name);
}

int main(void) {
    gen_textures();
    const char *dir = "assets/minecraft/textures/block";
    mkdirs(dir);
    printf("Exporting resource pack to %s/\n", dir);

    /* face index: 0 = top, 1 = side, 2 = bottom */
    write_tex(dir, "grass_block_top",  g_tex[B_GRASS][0], 1);   /* tinted green at load */
    write_tex(dir, "grass_block_side", g_tex[B_GRASS][1], 0);
    write_tex(dir, "dirt",             g_tex[B_DIRT][0],  0);
    write_tex(dir, "stone",            g_tex[B_STONE][0], 0);
    write_tex(dir, "cobblestone",      g_tex[B_COBBLE][0], 0);
    write_tex(dir, "oak_log",          g_tex[B_LOG][1],   0);
    write_tex(dir, "oak_log_top",      g_tex[B_LOG][0],   0);
    write_tex(dir, "oak_leaves",       g_tex[B_LEAVES][0], 1);  /* tinted green at load */
    write_tex(dir, "sand",             g_tex[B_SAND][0],  0);
    write_tex(dir, "oak_planks",       g_tex[B_PLANKS][0], 0);
    write_tex(dir, "water_still",      g_tex[B_WATER][0], 0);
    write_tex(dir, "glass",            g_tex[B_GLASS][0], 0);

    printf("done.\n");
    return 0;
}
