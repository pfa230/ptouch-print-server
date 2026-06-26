#include <assert.h>
#include <string.h>
#include "raster.h"

void pt_pack_line(uint8_t out[16], const uint8_t *in, int width)
{
    assert(width >= 0 && width <= 128);
    memset(out, 0, 16);
    int offset = (128 - width) / 2;
    for (int i = 0; i < width; i++) {
        if ((in[i / 8] >> (7 - (i % 8))) & 1) {  /* input MSB-first */
            int p = offset + i;
            out[15 - (p / 8)] |= (uint8_t)(1 << (p % 8));
        }
    }
}
