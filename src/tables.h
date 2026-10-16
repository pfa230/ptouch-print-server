#ifndef PT_TABLES_H
#define PT_TABLES_H

#include <stdint.h>
#include <stdbool.h>

/* Per-model protocol flags (from upstream ptouch-print). */
#define FLAG_NONE            0x00u
#define FLAG_RASTER_PACKBITS 0x01u  /* sendraster wraps a fake-PackBits run */
#define FLAG_P700_INIT       0x02u  /* rasterstart uses "1b 69 61 01" */
#define FLAG_PLITE           0x04u  /* P-Lite mode (not driven) */

typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    const char *name;
    int         max_px;   /* print-head width in dots */
    int         dpi;
    uint32_t    flags;
} pt_dev;

/* Look up a P-touch device by USB id; NULL if unknown. */
const pt_dev *pt_lookup_dev(uint16_t vid, uint16_t pid);

/* Look up a P-touch device by model name (e.g. "PT-2730"); NULL if unknown. */
const pt_dev *pt_lookup_name(const char *name);

/* Imageable px for a tape width in mm; 0 if unknown. */
int pt_tape_px(int mm);

/* Roll length range, in 1/100 mm, advertised in media-col. */
#define PT_ROLL_MIN_CENTIMM 500     /* 5 mm   */
#define PT_ROLL_MAX_CENTIMM 30000   /* 300 mm */

/* PT-P710BT reserves 14 dots of feed at each end of every page; 198 is the
 * smallest integral 1/100 mm that still maps back to 14 dots at 180 dpi. */
#define PT_END_MARGIN_PX 14
#define PT_END_MARGIN_CENTIMM 198
/* Documented minimum physical feed length for a P710BT label. */
#define PT_P710BT_MIN_CENTIMM 2450

/* Canonical 180dpi window shared by the supported models. right_px is exclusive;
 * margin_centimm is the physical minimum, with pixel-aligned published margins.
 * `pt_model_geometry` adds the model's end feed and usable length range. */
typedef struct {
    int nominal_px, imageable_px, left_px, right_px, margin_centimm;
    int left_margin_centimm, right_margin_centimm;
    int end_margin_px;          /* feed dots reserved at each tape end */
    int top_margin_centimm, bottom_margin_centimm;  /* published end margins */
    int min_centimm, max_centimm;   /* usable physical page length */
    bool pad_below_min;         /* legacy PNG 5 mm padding vs. reject */
} pt_geometry;
bool pt_tape_geometry(int tape_mm, pt_geometry *out);

/* Model-aware geometry: tape window plus the selected model's end margins and
 * usable length range. One definition used by publication, preflight, output. */
bool pt_model_geometry(const pt_dev *model, int tape_mm, pt_geometry *out);

#endif /* PT_TABLES_H */
