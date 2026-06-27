#ifndef PT_TABLES_H
#define PT_TABLES_H

#include <stdint.h>

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

#endif /* PT_TABLES_H */
