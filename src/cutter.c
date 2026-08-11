#include "cutter.h"

int pt_plan_batch(int n_pages, uint8_t tape_mm, pt_cut_mode mode, int precut,
                  bool chain_out, pt_op *out, int cap)
{
    int k = 0;
#define EMIT(K, A, P)                                  \
    do {                                               \
        if (k >= cap) return -1;                       \
        out[k].kind = (K);                             \
        out[k].arg = (uint8_t)(A);                     \
        out[k].page_index = (P);                       \
        k++;                                           \
    } while (0)

    for (int i = 0; i < n_pages; i++) {
        /* Two distinct meanings, deliberately not one flag: `last_page` is only
         * about indexing this batch, `terminal` is "last label of the whole run".
         * Folding chain_out into `final` would leave `do_precut` forcing autocut
         * on for PT_CUT_END, turning `end` into `each` at every job boundary. */
        int last_page = (i == n_pages - 1);
        int terminal = last_page && !chain_out;
        int cut_after = (mode == PT_CUT_EACH) || (mode == PT_CUT_END && terminal);
        int do_precut = precut && (mode != PT_CUT_NONE);
        int needs_auto_cut = do_precut || (cut_after && !terminal);

        if (needs_auto_cut) {
            EMIT(PT_OP_PRINTINFO, tape_mm, i);
        }
        EMIT(PT_OP_SETMODE, needs_auto_cut ? 0x40 : 0x00, i);

        int want_chain = (!terminal) || !cut_after;
        EMIT(PT_OP_SETADVANCED, want_chain ? 0x00 : 0x08, i);

        EMIT(PT_OP_PRINT_PAGE, 0, i);

        if (cut_after && terminal) {
            EMIT(PT_OP_EJECT, 0, i);
        } else {
            EMIT(PT_OP_FF, 0, i);
        }
    }
    return k;
#undef EMIT
}
