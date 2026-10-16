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

    /* PT-2730 keeps its cross-tape window, 5 mm minimum, and PNG padding. */
    const pt_dev *pt2730 = pt_lookup_name("PT-2730");
    assert(pt_model_geometry(pt2730, 12, &g));
    assert(g.end_margin_px == 0 && g.top_margin_centimm == 0 && g.bottom_margin_centimm == 0);
    assert(g.min_centimm == 500 && g.max_centimm == 30000 && g.pad_below_min);
    assert(g.nominal_px == 85 && g.imageable_px == 76);

    /* PT-P710BT reserves 14 dots per end and a 24.5 mm physical minimum. */
    assert(pt_model_geometry(p710bt, 12, &g));
    assert(g.end_margin_px == 14 && g.top_margin_centimm == 198 && g.bottom_margin_centimm == 198);
    assert(g.min_centimm == 2450 && g.max_centimm == 30000 && !g.pad_below_min);
    /* 198 centimm is the smallest value mapping back to 14 dots at 180 dpi. */
    assert(198 * 180 / 2540 == 14 && 197 * 180 / 2540 == 13);
    /* The minimum and maximum remain distinct and inside the roll range. */
    assert(g.min_centimm > 2 * g.top_margin_centimm && g.min_centimm < g.max_centimm);
    assert(!pt_model_geometry(NULL, 12, &g));
    assert(!pt_model_geometry(p710bt, 36, &g));
    return 0;
}
