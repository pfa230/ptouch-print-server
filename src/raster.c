#include <assert.h>
#include <string.h>
#include "raster.h"

void pt_pack_window(uint8_t out[16], const uint8_t *in, int start, int width)
{
    assert(start >= 0 && width >= 0 && width <= 128);
    memset(out, 0, 16);
    int offset = (128 - width) / 2;
    for (int i = 0; i < width; i++) {
        int s = start + i;
        if ((in[s / 8] >> (7 - (s % 8))) & 1) {  /* input MSB-first */
            int p = offset + i;
            out[15 - (p / 8)] |= (uint8_t)(1 << (p % 8));
        }
    }
}

void pt_pack_line(uint8_t out[16], const uint8_t *in, int width)
{
    pt_pack_window(out, in, 0, width);
}
