#include <assert.h>
#include <stddef.h>
#include "tables.h"

int main(void) {
    const pt_dev *d = pt_lookup_dev(0x04f9, 0x2041);
    assert(d && d->max_px == 128 && d->dpi == 180);
    assert(pt_lookup_dev(0x04f9, 0x9999) == NULL);

    /* per-model flags are carried */
    const pt_dev *p700 = pt_lookup_dev(0x04f9, 0x2061);
    assert(p700 && (p700->flags & FLAG_RASTER_PACKBITS));
    assert((d->flags & FLAG_RASTER_PACKBITS) == 0); /* PT-2730 is FLAG_NONE */

    /* tape mm -> imageable px */
    assert(pt_tape_px(24) == 128);
    assert(pt_tape_px(18) == 120);
    assert(pt_tape_px(12) == 76);
    assert(pt_tape_px(9)  == 52);
    assert(pt_tape_px(6)  == 32);
    assert(pt_tape_px(99) == 0);
    return 0;
}
