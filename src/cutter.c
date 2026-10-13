#include "cutter.h"

pt_cut_policy pt_cut_policy_for(pt_cut_mode mode)
{
    return (pt_cut_policy){
        .cut_between = mode == PT_CUT_CHAIN || mode == PT_CUT_EACH,
        .cut_at_end = mode == PT_CUT_EACH || mode == PT_CUT_END,
    };
}

bool pt_cut_autocut(pt_cut_policy policy, bool first_label)
{
    return first_label || policy.cut_between;
}
