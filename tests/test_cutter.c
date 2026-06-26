#include <assert.h>
#include "cutter.h"

int main(void) {
    pt_op ops[64];

    /* EACH, precut=1, n=3: per label PRINTINFO,SETMODE,SETADVANCED,PRINT,<feed> = 15 ops.
     * labels 0,1 chain (0x00) + FF; final chains-off (0x08) + EJECT. */
    int n = pt_plan_batch(3, 12, PT_CUT_EACH, 1, ops, 64);
    assert(n == 15);
    assert(ops[0].kind == PT_OP_PRINTINFO && ops[0].arg == 12);
    assert(ops[1].kind == PT_OP_SETMODE && ops[1].arg == 0x40);
    assert(ops[2].kind == PT_OP_SETADVANCED && ops[2].arg == 0x00);
    assert(ops[3].kind == PT_OP_PRINT_PAGE && ops[3].page_index == 0);
    assert(ops[4].kind == PT_OP_FF);
    /* final label (page 2) at ops[10..14] */
    assert(ops[12].kind == PT_OP_SETADVANCED && ops[12].arg == 0x08);
    assert(ops[13].kind == PT_OP_PRINT_PAGE && ops[13].page_index == 2);
    assert(ops[14].kind == PT_OP_EJECT);

    /* single label: precut + chain-off + eject (the "final-label combo") */
    n = pt_plan_batch(1, 12, PT_CUT_EACH, 1, ops, 64);
    assert(n == 5);
    assert(ops[0].kind == PT_OP_PRINTINFO);
    assert(ops[1].kind == PT_OP_SETMODE && ops[1].arg == 0x40);
    assert(ops[2].kind == PT_OP_SETADVANCED && ops[2].arg == 0x08);
    assert(ops[4].kind == PT_OP_EJECT);

    /* NONE: no printinfo, setmode 0x00, chain 0x00, FF (no eject) — 4 ops/label */
    n = pt_plan_batch(2, 12, PT_CUT_NONE, 1, ops, 64);
    assert(n == 8);
    assert(ops[0].kind == PT_OP_SETMODE && ops[0].arg == 0x00);
    assert(ops[1].kind == PT_OP_SETADVANCED && ops[1].arg == 0x00);
    assert(ops[2].kind == PT_OP_PRINT_PAGE);
    assert(ops[3].kind == PT_OP_FF);
    assert(ops[7].kind == PT_OP_FF);

    /* cap too small -> -1 */
    assert(pt_plan_batch(3, 12, PT_CUT_EACH, 1, ops, 4) == -1);
    return 0;
}
