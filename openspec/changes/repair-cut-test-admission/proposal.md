# Proposal

## Why

PR #66's container test fails at tests/test_driver.c:1607 because the canceled-stream fixture withholds all document bytes while waiting for job admission. PAPPL 1.4.11 checks for document data before creating a Print-Job, so that admission cannot happen.

Fixes #65

## What Changes

- Repair the canceled-stream fixture so the server receives enough document data to admit the job, while the test still controls the remaining input needed to keep canceled completion behind the cut.
- Run the full adapter container tests after each implementation, before this change's implementation review and archival.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This repairs verification of the existing queued-cut contract; skip_specs is true.

## Impact

Only the canceled-stream test fixture and its supporting helper in tests/test_driver.c are in scope. The queued-cut implementation and accepted specifications are unchanged. Delivery updates the existing issue-65-queue-cut branch and PR #66.

This repair builds on the existing queued-cut implementation at `ed1a03d` (PR #66). Implementation review must compare against that commit (`git diff ed1a03d HEAD`). The loop-derived `merge-base(HEAD, origin/main)` is currently `1c1ccca`, which also includes the unlanded queued-cut predecessor in the review range; those predecessor changes are outside this repair's scope.
