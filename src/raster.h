#ifndef PT_RASTER_H
#define PT_RASTER_H

#include <stdint.h>

/* Lengthwise leading-blank quirk; tuned on hardware in M3 (the PT-2730 was
 * reported to need leading whitespace before content prints). It is a driver-side
 * feed concern, not part of per-line packing. */
#define PT_LEADER_PX 0

/* Center a 1-bit input scanline into the 16-byte (128-dot) Brother raster line.
 * `in` is packed 1-bit, MSB-first, `width` bits, bit set = black (PWG black_1).
 * Input bit i maps to head dot offset+i, offset = (128 - width) / 2.
 * `width` must be in [0, 128]. The core does NOT threshold — the renderer
 * (labeler / PAPPL bi-level) owns gray->B/W. */
void pt_pack_line(uint8_t out[16], const uint8_t *in, int width);

#endif /* PT_RASTER_H */
