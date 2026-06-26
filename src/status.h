#ifndef PT_STATUS_H
#define PT_STATUS_H

#include <stdint.h>

/* Parsed Brother 32-byte status block. `error` and `raw` are exposed so the
 * adapter (M2/M3) can derive printer-state-reasons without an ABI change; the
 * exact error-bit meanings (no-media, cover-open, cutter-jam, …) come from the
 * Brother error tables and are confirmed on hardware in M3. */
typedef struct {
    int      tape_mm;   /* byte 10 */
    int      tape_px;   /* via pt_tape_px(); 0 if unknown */
    uint16_t error;     /* bytes 8-9, little-endian */
    uint8_t  raw[32];   /* the full status block */
} pt_status;

/* Parse a 32-byte status block. Returns 0 if the header is valid
 * (byte0 == 0x80, byte1 == 0x20), else -1. */
int pt_parse_status(const uint8_t buf[32], pt_status *s);

#endif /* PT_STATUS_H */
