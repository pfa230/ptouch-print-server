#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "tables.h"

int main(void) {
    const pt_dev *d = pt_lookup_dev(0x04f9, 0x2041);
    assert(d && d->max_px == 128 && d->dpi == 180);
    assert(pt_lookup_name("PT-2730") == d && d->flags == FLAG_NONE);
    assert(pt_lookup_dev(0x04f9, 0x9999) == NULL);
    assert(pt_lookup_dev(0xffff, 0x20af) == NULL);
    assert(pt_lookup_name("PT-unknown") == NULL && pt_lookup_name(NULL) == NULL);

    const pt_dev *p710bt = pt_lookup_dev(0x04f9, 0x20af);
    assert(p710bt && strcmp(p710bt->name, "PT-P710BT") == 0);
    assert(p710bt->max_px == 128 && p710bt->dpi == 180);
    assert(p710bt->flags == (FLAG_RASTER_PACKBITS | FLAG_P700_INIT));
    assert(pt_lookup_name("PT-P710BT") == p710bt);

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
    const int widths[] = {6, 9, 12, 18, 24};
    const int nominal[] = {42, 63, 85, 127, 170};
    const int imageable[] = {32, 52, 76, 120, 128};
    for (unsigned i = 0; i < sizeof widths / sizeof widths[0]; i++) {
        pt_geometry g;
        assert(pt_tape_geometry(widths[i], &g));
        assert(g.nominal_px == nominal[i] && g.imageable_px == imageable[i]);
        assert(g.left_px == (nominal[i] - imageable[i]) / 2);
        assert(g.right_px - g.left_px == imageable[i] && g.right_px <= nominal[i]);
        assert(g.margin_centimm >= 53);
    }
    pt_geometry g;
    assert(pt_tape_geometry(18, &g) && g.margin_centimm == 53);
    assert(!pt_tape_geometry(36, &g) && !pt_tape_geometry(99, &g));
    return 0;
}
