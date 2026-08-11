#include <assert.h>
#include "cutter.h"

int main(void) {
    pt_op ops[64];

    /* EACH, precut=1, n=3: per label PRINTINFO,SETMODE,SETADVANCED,PRINT,<feed> = 15 ops.
     * labels 0,1 chain (0x00) + FF; final chains-off (0x08) + EJECT. */
    int n = pt_plan_batch(3, 12, PT_CUT_EACH, 1, false, ops, 64);
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
    n = pt_plan_batch(1, 12, PT_CUT_EACH, 1, false, ops, 64);
    assert(n == 5);
    assert(ops[0].kind == PT_OP_PRINTINFO);
    assert(ops[1].kind == PT_OP_SETMODE && ops[1].arg == 0x40);
    assert(ops[2].kind == PT_OP_SETADVANCED && ops[2].arg == 0x08);
    assert(ops[4].kind == PT_OP_EJECT);

    /* NONE: no printinfo, setmode 0x00, chain 0x00, FF (no eject) — 4 ops/label */
    n = pt_plan_batch(2, 12, PT_CUT_NONE, 1, false, ops, 64);
    assert(n == 8);
    assert(ops[0].kind == PT_OP_SETMODE && ops[0].arg == 0x00);
    assert(ops[1].kind == PT_OP_SETADVANCED && ops[1].arg == 0x00);
    assert(ops[2].kind == PT_OP_PRINT_PAGE);
    assert(ops[3].kind == PT_OP_FF);
    assert(ops[7].kind == PT_OP_FF);

    /* cap too small -> -1 */
    assert(pt_plan_batch(3, 12, PT_CUT_EACH, 1, false, ops, 4) == -1);

    /* --- chain_out: another job follows, so the last label is not terminal (#41) --- */

    /* EACH, n=1, precut=1, chained: autocut STAYS ON (the boundary between this
     * label and the next job's must still be cut), chain 0x00, finish FF, no EJECT. */
    n = pt_plan_batch(1, 12, PT_CUT_EACH, 1, true, ops, 64);
    assert(n == 5);
    assert(ops[0].kind == PT_OP_PRINTINFO && ops[0].arg == 12);
    assert(ops[1].kind == PT_OP_SETMODE && ops[1].arg == 0x40);
    assert(ops[2].kind == PT_OP_SETADVANCED && ops[2].arg == 0x00);
    assert(ops[3].kind == PT_OP_PRINT_PAGE);
    assert(ops[4].kind == PT_OP_FF);

    /* EACH, n=3, chained: only the last label differs from the unchained plan. */
    n = pt_plan_batch(3, 12, PT_CUT_EACH, 1, true, ops, 64);
    assert(n == 15);
    assert(ops[11].kind == PT_OP_SETMODE && ops[11].arg == 0x40);
    assert(ops[12].kind == PT_OP_SETADVANCED && ops[12].arg == 0x00);
    assert(ops[14].kind == PT_OP_FF);
    for (int i = 0; i < n; i++)
        assert(ops[i].kind != PT_OP_EJECT);

    /* END + precut=1, chained: finish FF and chain 0x00, and autocut is left exactly
     * as today's code produces it. do_precut already forces SETMODE 0x40 on every
     * label in `end` mode (a pre-existing defect, #42) - assert that, not the
     * aspiration, so this test cannot mask a chain-induced change. */
    n = pt_plan_batch(1, 12, PT_CUT_END, 1, true, ops, 64);
    assert(n == 5);
    assert(ops[0].kind == PT_OP_PRINTINFO);
    assert(ops[1].kind == PT_OP_SETMODE && ops[1].arg == 0x40);
    assert(ops[2].kind == PT_OP_SETADVANCED && ops[2].arg == 0x00);
    assert(ops[4].kind == PT_OP_FF);

    /* END + precut=0, chained: no autocut at a job boundary, finish FF. */
    n = pt_plan_batch(1, 12, PT_CUT_END, 0, true, ops, 64);
    assert(n == 4);
    assert(ops[0].kind == PT_OP_SETMODE && ops[0].arg == 0x00);
    assert(ops[1].kind == PT_OP_SETADVANCED && ops[1].arg == 0x00);
    assert(ops[3].kind == PT_OP_FF);

    /* NONE ignores chain_out entirely. */
    pt_op none_ops[64];
    int n_none = pt_plan_batch(2, 12, PT_CUT_NONE, 1, false, none_ops, 64);
    n = pt_plan_batch(2, 12, PT_CUT_NONE, 1, true, ops, 64);
    assert(n == n_none);
    for (int i = 0; i < n; i++)
        assert(ops[i].kind == none_ops[i].kind && ops[i].arg == none_ops[i].arg);
    return 0;
}
