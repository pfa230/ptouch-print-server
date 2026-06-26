#include <assert.h>
#include <string.h>
#include "protocol.h"
#include "tables.h"

int main(void) {
    uint8_t o[64];

    assert(pt_cmd_init(o) == 2 && memcmp(o, "\x1b\x40", 2) == 0);

    /* rasterstart differs for P700-class models */
    assert(pt_cmd_rasterstart(o, FLAG_NONE) == 4 && memcmp(o, "\x1b\x69\x52\x01", 4) == 0);
    assert(pt_cmd_rasterstart(o, FLAG_P700_INIT) == 4 && memcmp(o, "\x1b\x69\x61\x01", 4) == 0);

    assert(pt_cmd_ff(o) == 1 && o[0] == 0x0c);
    assert(pt_cmd_eject(o) == 1 && o[0] == 0x1a);

    assert(pt_cmd_printinfo(o, 0x18) == 8 &&
           memcmp(o, "\x1b\x69\x63\x84\x00\x18\x00\x00", 8) == 0);
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
