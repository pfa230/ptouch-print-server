#ifndef PT_CUTTER_H
#define PT_CUTTER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PT_CUT_EACH,   /* precut + cut after every label (chained between) */
    PT_CUT_END,    /* cut only after the final label */
    PT_CUT_NONE,   /* no cuts */
} pt_cut_mode;

typedef enum {
    PT_OP_PRINTINFO,
    PT_OP_SETMODE,
    PT_OP_SETADVANCED,
    PT_OP_PRINT_PAGE,
    PT_OP_FF,
    PT_OP_EJECT,
} pt_op_kind;

typedef struct {
    pt_op_kind kind;
    uint8_t    arg;        /* PRINTINFO: tape mm; SETMODE/SETADVANCED: flags */
    int        page_index; /* page this op belongs to */
} pt_op;

/* Plan the op sequence for an N-page batch, replicating the verified macOS-fork
 * `flush_print_job` logic. N comes from PAPPL pages/copies in one device session
 * (NOT multi-document). Returns the op count, or -1 if `cap` is too small.
 * Pure: the driver renders each op to bytes via protocol.c.
 *
 * chain_out: another job follows, so plan the LAST label like a non-final one
 * (SETADVANCED 0x00, finish FF) and leave the tape at the head instead of
 * ejecting (#41). Autocut keeps whatever `mode` + `precut` already produce -
 * this flag must not change it. Ignored for PT_CUT_NONE. */
int pt_plan_batch(int n_pages, uint8_t tape_mm, pt_cut_mode mode, int precut,
                  bool chain_out, pt_op *out, int cap);

#endif /* PT_CUTTER_H */
