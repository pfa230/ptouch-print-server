#include <stddef.h>
#include "tables.h"

/* P-touch device family (transcribed from hannesweisbach/ptouch-print
 * src/libptouch.c). Only the PT-2730 is hardware-certified here; the rest are
 * best-effort, untested. All known models are 180dpi / 128px max. */
static const pt_dev g_devs[] = {
    {0x04f9, 0x2007, "PT-2420PC",            128, 180, FLAG_RASTER_PACKBITS},
    {0x04f9, 0x202c, "PT-1230PC",            128, 180, FLAG_NONE},
    {0x04f9, 0x202d, "PT-2430PC",            128, 180, FLAG_NONE},
    {0x04f9, 0x2030, "PT-1230PC (PLite)",    128, 180, FLAG_PLITE},
    {0x04f9, 0x2031, "PT-2430PC (PLite)",    128, 180, FLAG_PLITE},
    {0x04f9, 0x2041, "PT-2730",              128, 180, FLAG_NONE},  /* certified */
    {0x04f9, 0x205f, "PT-E500",              128, 180, FLAG_RASTER_PACKBITS},
    {0x04f9, 0x205e, "PT-H500",              128, 180, FLAG_RASTER_PACKBITS | FLAG_P700_INIT},
    {0x04f9, 0x2061, "PT-P700",              128, 180, FLAG_RASTER_PACKBITS | FLAG_P700_INIT},
    {0x04f9, 0x2064, "PT-P700 (PLite)",      128, 180, FLAG_PLITE},
    {0x04f9, 0x2062, "PT-P750W",             128, 180, FLAG_RASTER_PACKBITS | FLAG_P700_INIT},
    {0x04f9, 0x2065, "PT-P750W (PLite)",     128, 180, FLAG_PLITE},
    {0x04f9, 0x2073, "PT-D450",              128, 180, FLAG_RASTER_PACKBITS},
    {0x04f9, 0x2074, "PT-D600",              128, 180, FLAG_RASTER_PACKBITS},
    {0, 0, NULL, 0, 0, 0},
};

/* Tape width (mm) -> imageable px (transcribed from the upstream tape table).
 * 3.5mm is out of v1 scope. */
static const struct { int mm; int px; } g_tape[] = {
    { 6,  32},
    { 9,  52},
    {12,  76},
    {18, 120},
    {24, 128},
    {36, 192},
    { 0,   0},
};

const pt_dev *pt_lookup_dev(uint16_t vid, uint16_t pid)
{
    for (const pt_dev *d = g_devs; d->name != NULL; d++) {
        if (d->vid == vid && d->pid == pid) {
            return d;
        }
    }
    return NULL;
}

int pt_tape_px(int mm)
{
    for (size_t i = 0; g_tape[i].mm != 0; i++) {
        if (g_tape[i].mm == mm) {
            return g_tape[i].px;
        }
    }
    return 0;
}
