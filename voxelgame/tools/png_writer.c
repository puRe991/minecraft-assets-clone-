/* Thin external wrapper around the repo's header-only PNG writer so the C++
 * visualizer can link against it (the header's functions are static). */
#define PNG_ENABLE_WRITE
#include "../../src/png.h"

int vg_png_write(const char *path, const unsigned int *pix, int w, int h) {
    return png_write(path, pix, w, h);
}
