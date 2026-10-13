#ifndef PT_CUTTER_H
#define PT_CUTTER_H

#include <stdbool.h>

typedef enum {
    PT_CUT_EACH,   /* cuts between labels and after the batch */
    PT_CUT_END,    /* cut only after the batch */
    PT_CUT_NONE,   /* no label cuts; leave the batch open */
    PT_CUT_CHAIN,  /* cuts between labels; leave the batch open */
} pt_cut_mode;

typedef struct {
    bool cut_between;
    bool cut_at_end;
} pt_cut_policy;

/* Pure policy, independent of copies or total label count. */
pt_cut_policy pt_cut_policy_for(pt_cut_mode mode);

/* Every nonempty batch has mandatory precut at its first actual label.
 * Later labels cut the preceding label only when the policy requests it. */
bool pt_cut_autocut(pt_cut_policy policy, bool first_label);

#endif /* PT_CUTTER_H */
