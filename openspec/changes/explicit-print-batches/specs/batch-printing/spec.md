# Queue-run printing

## Purpose

Print consecutive runnable jobs as one tape run and finalize when the queue drains. Client batch boundaries do not determine the run boundary; the server makes that decision after the current job's pages are submitted.

## ADDED Requirements

### Requirement: Continuation across queued jobs

For valid jobs on a healthy printer with matching tape, the server SHALL continue the tape run when another runnable job is queued at the current job's completion check. Under `PTOUCH_CUT_MODE=each` and `PTOUCH_PRECUT=1`, the run SHALL have one leader and labels cut apart. Client batch boundaries SHALL NOT force finalization.

#### Scenario: Follower arrives during printing
- **WHEN** a valid follower becomes runnable after the current label starts but before the current job's completion check
- **THEN** the jobs print in the same run without an intervening terminal eject or printer reset

#### Scenario: Different client batches are queued
- **WHEN** jobs from different client batches remain runnable at successive completion checks
- **THEN** they share a continuous run in the printer queue's existing processing order

### Requirement: Immediate finalization on queue drain

When a cutting-enabled job completes successfully with no other runnable job at its completion check, the server SHALL finalize the run while the finishing job still owns the device. Successful normal completion SHALL NOT wait for an idle grace period or timer. Held, incomplete, canceled, and stopped jobs SHALL NOT postpone this decision.

#### Scenario: Single label with empty queue
- **WHEN** one label finishes and no other job is runnable
- **THEN** its run is finalized immediately without an extra blank label or duplicate successful terminal eject

#### Scenario: Final job in a queue run
- **WHEN** the last runnable job finishes
- **THEN** the run is finalized once after its last label, with the final label's intended length preserved

#### Scenario: Only held or uploading work remains
- **WHEN** the finishing job's completion check finds only held or incomplete jobs
- **THEN** it finalizes the current run

### Requirement: Defined late-arrival boundary

A job becoming runnable after the completion-time queue check SHALL be permitted to start a new tape run and incur its own leader. The server SHALL NOT delay normal finalization to predict future submissions or guarantee one leader for a client batch separated by empty-queue completion checks.

#### Scenario: Follower arrives after the check
- **WHEN** a new job becomes runnable after the previous job decided to finalize its empty queue
- **THEN** the new job starts a new run after that finalization

### Requirement: Existing printing compatibility

The server SHALL retain existing advertised document formats, PNG geometry and copy limits, strict tape-width rejection, and offline queueing. Clients SHALL NOT need a new batch document or capability handshake. Normal `none` completion SHALL NOT gain a terminal cut. With precut disabled, the change SHALL NOT introduce a leader cut. Existing cut policies, including the known `end` plus precut limitation, SHALL remain documented.

#### Scenario: PNG copies followed by another job
- **WHEN** one PNG requests multiple supported copies and a valid follower is runnable at completion
- **THEN** all requested copies print before the follower and the run stays open between jobs

#### Scenario: No-cut mode
- **WHEN** jobs complete normally with `PTOUCH_CUT_MODE=none`
- **THEN** queue drain does not introduce an automatic terminal cut

#### Scenario: Precut disabled
- **WHEN** a single label or multiple copies print with `PTOUCH_CUT_MODE=each` and `PTOUCH_PRECUT=0`
- **THEN** the revised continuation and finalization sequence does not introduce a leader cut

#### Scenario: Jobs queued while offline
- **WHEN** accepted PNG jobs wait for the printer to return with matching tape
- **THEN** existing media confirmation and strict geometry checks still apply and runnable jobs share a queue run

### Requirement: Stranded-run cleanup

If the selected follower never prints, the server SHALL retain best-effort cleanup of the open run once no runnable or processing job remains. Cleanup SHALL NOT interleave terminal output with a printing job, and SHALL NOT emit another terminal eject for a run already finalized successfully.

#### Scenario: Follower canceled before startup
- **WHEN** a run was left open for a queued follower that is then canceled and no other job remains runnable
- **THEN** fallback cleanup attempts to detach the printed tape without depending on that canceled job printing

#### Scenario: Follower fails validation
- **WHEN** the queued follower fails before its driver callbacks start and no other job remains runnable
- **THEN** fallback cleanup attempts to finalize the stranded run

### Requirement: Detected output failure

A detected raster or terminal-command transfer failure SHALL prevent the current job from reporting successful completion. The server SHALL only record successful finalization after the terminal transfer succeeds. Failed cleanup SHALL remain eligible for best-effort recovery without automatically reprinting label content.

#### Scenario: Terminal transfer fails
- **WHEN** the terminal eject cannot be transferred
- **THEN** the job reports failure and the server does not record the run as successfully closed

#### Scenario: Abort cleanup repeats a callback
- **WHEN** abort handling repeats end-of-job cleanup after an output failure
- **THEN** no label content is resent and an already successful finalization is not repeated
