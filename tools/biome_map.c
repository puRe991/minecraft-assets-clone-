/* Renders a top-down biome map of the MiniCraft world generator to PNG,
 * to visualise the biome distribution (headless, no window). */
#define HEADLESS_TEST
#define PNG_ENABLE_WRITE
#include "../src/main.c"
#include <stdio.h>

static uint32_t biome_color(int b) {
    switch (b) {
        case BIO_OCEAN:        return rgb(40, 90, 170);
        case BIO_FROZEN_OCEAN: return rgb(150, 195, 230);
        case BIO_BEACH:        return rgb(222, 206, 140);
        case BIO_PLAINS:       return rgb(126, 196, 92);
        case BIO_FOREST:       return rgb(46, 120, 56);
        case BIO_BIRCH:        return rgb(140, 176, 104);
        case BIO_DARK_FOREST:  return rgb(28, 78, 40);
        case BIO_JUNGLE:       return rgb(42, 142, 40);
        case BIO_SAVANNA:      return rgb(178, 176, 84);
        case BIO_DESERT:       return rgb(224, 208, 150);
        case BIO_BADLANDS:     return rgb(192, 102, 56);
        case BIO_SWAMP:        return rgb(94, 112, 80);
        case BIO_TAIGA:        return rgb(90, 132, 104);
        case BIO_SNOWY:        return rgb(236, 240, 246);
        case BIO_MOUNTAINS:    return rgb(140, 140, 146);
        case BIO_MUSHROOM:     return rgb(156, 120, 152);
        default:               return rgb(255, 0, 255);
    }
}

#define W 480
#define H 270
#define BPP 4      /* world blocks per pixel */
int main(void) {
    g_seed = 2024;
    const int bpp = BPP;
    static uint32_t img[W * H];
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            int wx = (i - W / 2) * bpp, wz = (j - H / 2) * bpp;
            img[j * W + i] = biome_color(biome_at(wx, wz));
        }
    int scale = 2, BW = W * scale, BH = H * scale;
    static uint32_t big[W * 2 * H * 2];
    for (int y = 0; y < BH; y++)
        for (int x = 0; x < BW; x++)
            big[y * BW + x] = img[(y / scale) * W + (x / scale)] | 0xff000000u;
    system("mkdir -p screenshots");
    png_write("screenshots/biome_map.png", big, BW, BH);
    printf("wrote screenshots/biome_map.png (%dx%d, %d blocks across)\n", BW, BH, W * bpp);
    return 0;
}
