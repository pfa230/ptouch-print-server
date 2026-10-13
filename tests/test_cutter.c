#include <assert.h>
#include "cutter.h"

int main(void)
{
    const struct {
        pt_cut_mode mode;
        bool between, at_end;
    } cases[] = {
        {PT_CUT_CHAIN, true, false},
        {PT_CUT_EACH, true, true},
        {PT_CUT_NONE, false, false},
        {PT_CUT_END, false, true},
    };
    const int counts[] = {1, 3, 65, 999};

    for (unsigned m = 0; m < sizeof cases / sizeof cases[0]; m++) {
        pt_cut_policy policy = pt_cut_policy_for(cases[m].mode);
        assert(policy.cut_between == cases[m].between);
        assert(policy.cut_at_end == cases[m].at_end);

        for (unsigned c = 0; c < sizeof counts / sizeof counts[0]; c++) {
            int autocuts = 0;
            for (int label = 0; label < counts[c]; label++) {
                bool autocut = pt_cut_autocut(policy, label == 0);
                assert(autocut == (label == 0 || cases[m].between));
                autocuts += autocut;
            }
            /* Mandatory precut happens once, even for no-cut/final-only. */
            assert(autocuts == (cases[m].between ? counts[c] : 1));
        }

        /* The next batch starts with precut even on a healthy open run. */
        assert(pt_cut_autocut(policy, true));
    }
    return 0;
}
