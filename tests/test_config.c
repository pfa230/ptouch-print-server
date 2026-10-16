#include <assert.h>
#include <string.h>
#include "config.h"

int main(void)
{
    int seconds = 7;
    assert(pt_parse_idle_poll(NULL, &seconds) && seconds == -1);
    assert(pt_parse_idle_poll("", &seconds) && seconds == -1);
    assert(pt_parse_idle_poll("0", &seconds) && seconds == 0);
    assert(pt_parse_idle_poll("300", &seconds) && seconds == 300);
    const char *bad_polls[] = {"-1", "1.5", "5s", " 5", "abc", "99999999999"};
    for (unsigned i = 0; i < sizeof bad_polls / sizeof bad_polls[0]; i++)
        assert(!pt_parse_idle_poll(bad_polls[i], &seconds));

    uint8_t address[6];
    const uint8_t expected[6] = {0xA4, 0xC1, 0x38, 0xAB, 0xCD, 0xEF};
    assert(pt_parse_bt_address("A4:C1:38:AB:CD:EF", address) && !memcmp(address, expected, 6));
    assert(pt_parse_bt_address("a4:c1:38:ab:cd:ef", address) && !memcmp(address, expected, 6));
    const char *bad_addresses[] = {NULL, "", "A4:C1:38:AB:CD", "A4:C1:38:AB:CD:EF:", "A4-C1-38-AB-CD-EF",
                                   "A4:C1:38:AB:CD:EG", "A4:C1:38:AB:CD:E", "A4C138ABCDEF"};
    for (unsigned i = 0; i < sizeof bad_addresses / sizeof bad_addresses[0]; i++)
        assert(!pt_parse_bt_address(bad_addresses[i], address));
    return 0;
}
