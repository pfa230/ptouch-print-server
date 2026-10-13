#include <assert.h>
#include <string.h>
#include "protocol.h"
#include "tables.h"

int main(void) {
    uint8_t o[128];

    memset(o, 0xa5, sizeof o);
    assert(pt_cmd_invalidate(o) == 100);
    for (unsigned i = 0; i < 100; i++) assert(o[i] == 0);
    assert(o[100] == 0xa5);

    assert(pt_cmd_init(o) == 2 && memcmp(o, "\x1b\x40", 2) == 0);

    /* rasterstart differs for P700-class models */
    assert(pt_cmd_rasterstart(o, FLAG_NONE) == 4 && memcmp(o, "\x1b\x69\x52\x01", 4) == 0);
    assert(pt_cmd_rasterstart(o, FLAG_P700_INIT) == 4 && memcmp(o, "\x1b\x69\x61\x01", 4) == 0);

    assert(pt_cmd_ff(o) == 1 && o[0] == 0x0c);
    assert(pt_cmd_eject(o) == 1 && o[0] == 0x1a);

    assert(pt_cmd_printinfo(o, 0x18) == 8 &&
           memcmp(o, "\x1b\x69\x63\x84\x00\x18\x00\x00", 8) == 0);
    /* The tested first page and a height using all four little-endian bytes. */
    memset(o, 0xa5, sizeof o);
    assert(pt_cmd_printinfo_extended(o, 12, 180, 0) == 13 &&
           memcmp(o, "\x1b\x69\x7a\x84\x00\x0c\x00\xb4\x00\x00\x00\x00\x00", 13) == 0);
    assert(o[13] == 0xa5);
    assert(pt_cmd_printinfo_extended(o, 18, 0x12345678u, 1) == 13 &&
           memcmp(o, "\x1b\x69\x7a\x84\x00\x12\x00\x78\x56\x34\x12\x01\x00", 13) == 0);
    assert(pt_cmd_printinfo_extended(o, 9, UINT32_MAX, 0) == 13 &&
           memcmp(o + 7, "\xff\xff\xff\xff\x00\x00", 6) == 0);
    assert(pt_cmd_printinfo_extended(o, 6, 0, 1) == 13 &&
           memcmp(o + 7, "\x00\x00\x00\x00\x01\x00", 6) == 0);
    assert(pt_cmd_compression(o, 2) == 2 && memcmp(o, "\x4d\x02", 2) == 0);
    assert(pt_cmd_compression(o, 0) == 2 && memcmp(o, "\x4d\x00", 2) == 0);
    assert(pt_cmd_auto_status(o, true) == 4 && memcmp(o, "\x1b\x69\x21\x00", 4) == 0);
    assert(pt_cmd_auto_status(o, false) == 4 && memcmp(o, "\x1b\x69\x21\x01", 4) == 0);
    assert(pt_cmd_setmode(o, 0x40) == 4 && memcmp(o, "\x1b\x69\x4d\x40", 4) == 0);
    assert(pt_cmd_setadvanced(o, 0x08) == 4 && memcmp(o, "\x1b\x69\x4b\x08", 4) == 0);

    assert(pt_cmd_status_request(o) == 3 && memcmp(o, "\x1b\x69\x53", 3) == 0);
    assert(pt_cmd_margin(o, 0x000e) == 5 && memcmp(o, "\x1b\x69\x64\x0e\x00", 5) == 0);

    uint8_t row[2] = {0xAB, 0xCD};
    assert(pt_cmd_sendraster(o, row, 2) == 5 && memcmp(o, "\x47\x02\x00\xAB\xCD", 5) == 0);
    assert(pt_cmd_sendraster_packbits(o, row, 2) == 6 &&
           memcmp(o, "\x47\x03\x00\x01\xAB\xCD", 6) == 0);
    return 0;
}
