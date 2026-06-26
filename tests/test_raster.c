#include <assert.h>
#include <string.h>
#include "raster.h"

static int bitset(const uint8_t o[16], int p) { return (o[15 - (p / 8)] >> (p % 8)) & 1; }
static void setin(uint8_t *in, int i) { in[i / 8] |= (uint8_t)(1 << (7 - (i % 8))); } /* MSB-first */

int main(void) {
    uint8_t out[16], in[16];

    /* width 76 (12mm): black at input bit 0 -> head dot (128-76)/2 = 26 */
    memset(in, 0, 16); setin(in, 0);
    pt_pack_line(out, in, 76);
    assert(bitset(out, 26) == 1 && !bitset(out, 25) && !bitset(out, 27));

    /* last input bit (75) -> dot 26+75 = 101 */
    memset(in, 0, 16); setin(in, 75);
    pt_pack_line(out, in, 76);
    assert(bitset(out, 101) == 1);

    /* all-white input -> all-zero output */
    memset(in, 0, 16);
    pt_pack_line(out, in, 76);
    uint8_t zero[16] = {0};
    assert(memcmp(out, zero, 16) == 0);

    /* full-width black (128) -> all 0xFF */
    memset(in, 0xFF, 16);
    pt_pack_line(out, in, 128);
    uint8_t ones[16]; memset(ones, 0xFF, 16);
    assert(memcmp(out, ones, 16) == 0);

    return 0;
}
