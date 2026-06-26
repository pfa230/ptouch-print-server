#include <assert.h>
#include <string.h>
#include "status.h"

static void mkstatus(uint8_t b[32], uint8_t mm, uint8_t e8, uint8_t e9) {
    memset(b, 0, 32);
    b[0] = 0x80; b[1] = 0x20; b[2] = 'B';  /* valid header */
    b[8] = e8; b[9] = e9; b[10] = mm;
}

int main(void) {
    uint8_t b[32];
    pt_status s;

    /* valid 24mm */
    mkstatus(b, 0x18, 0, 0);
    assert(pt_parse_status(b, &s) == 0 && s.tape_mm == 24 && s.tape_px == 128 && s.error == 0);

    /* valid 12mm */
    mkstatus(b, 0x0c, 0, 0);
    assert(pt_parse_status(b, &s) == 0 && s.tape_mm == 12 && s.tape_px == 76);

    /* bad header -> rejected */
    mkstatus(b, 0x18, 0, 0); b[0] = 0x00;
    assert(pt_parse_status(b, &s) == -1);

    /* unknown tape width -> px 0, still valid */
    mkstatus(b, 0x07, 0, 0);
    assert(pt_parse_status(b, &s) == 0 && s.tape_mm == 7 && s.tape_px == 0);

    /* error bytes -> little-endian error field; raw preserved */
    mkstatus(b, 0x0c, 0x01, 0x10);
    assert(pt_parse_status(b, &s) == 0 && s.error == 0x1001 && s.raw[10] == 0x0c && s.raw[0] == 0x80);

    return 0;
}
