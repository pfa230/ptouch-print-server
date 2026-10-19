# Design

## Context

See proposal.md for the observed race. `pt_cut_op_cb` currently calls `papplPrinterOpenDevice`, which refuses whenever `device_in_use` or `processing_job` is set. PAPPL 1.4.11 starts the next queued job from `_papplPrinterCheckJobsNoLock` and job completion; raster streaming can enter `start_job` directly. Consequently, retrying OpenDevice in the adapter cannot reserve a position between prints.

The pinned source was inspected at tag v1.4.11, commit `ad86a0a8473f0f83233a374226561181570d4f81`: [device ownership](https://github.com/michaelrsweet/pappl/blob/v1.4.11/pappl/printer-accessors.c), [scheduling](https://github.com/michaelrsweet/pappl/blob/v1.4.11/pappl/job.c), and [job start and completion](https://github.com/michaelrsweet/pappl/blob/v1.4.11/pappl/job-process.c). PAPPL assigns increasing per-printer job IDs under its printer lock and retains active jobs, which supplies the ordering boundary without copying jobs or raster data.

## Goals / Non-Goals

**Goals:** Reserve a cut's position within PAPPL's ownership and job lifecycle, using its existing lock and active-job accounting.

**Non-Goals:** A second scheduler, synthetic jobs, durable command storage, new queue settings, client-side retries, or changes to cutter bytes. Existing issue #62's driver-test exit delays remain outside this change.

## Decisions

### Queue ownership inside PAPPL

Extend the existing `pappl-1.4.11-printer-operations.patch` with a queued-acquisition entrypoint, `papplPrinterOpenDeviceQueued`, declared in the public printer header. Keep the existing nonblocking acquisition path for status reads. After existing validation and printing authorization, the cut handler calls the queued entrypoint and uses its existing cut and close paths.

Under the printer lock, append a waiting acquisition containing the last assigned job ID and its FIFO link. A printer-owned head pointer locates the waiting acquisitions. The oldest waiter is eligible only after all earlier accepted jobs have left the active set and current device ownership is released. Its boundary is PAPPL admission, not packet-arrival or response-delivery time. Earlier held, incompletely submitted, or offline jobs consequently delay a cut until they finish or are canceled.

Gate later job starts against the oldest waiter's job-ID boundary, including the direct raster `start_job` entry. The ownership handoff must stay under the same printer lock as job completion and device cleanup; otherwise completion can close a device already handed to the cut. Later print jobs remain PAPPL jobs with their usual state and scheduling. Idle polling must not acquire ahead of an eligible waiting cut.

Use PAPPL's existing unlock/wait/recheck style rather than introducing another worker or event loop. Pop the waiting record when ownership is acquired or the wait exits, and resume PAPPL's normal scheduling when that barrier is removed. The record belongs to the active synchronous request. Existing system shutdown and printer deletion stop the wait and release its record before the printer is destroyed.

A plain adapter mutex covers only cuts, so prints can overtake it. A loop around nonblocking OpenDevice has the same problem. A synthetic print job would create a job/document lifecycle for an action the existing architecture explicitly keeps independent of jobs. The narrow PAPPL change places ordering alongside the code that actually starts work.

### Preserve the operation's result

The request stays open while waiting and reports the existing `pt_cut` outcome once executed. A failed transport open at its turn still returns `server-error-busy`; this change does not add retries for physical unavailability. Run state and live tape are evaluated when ownership is obtained, so earlier printing determines what the cut releases.

Returning success as soon as a cut is scheduled would falsely report physical completion to Labeler and MCP. It would also require a separate way to retrieve failures. Keeping synchronous completion avoids that interface change. A lost response still leaves the caller with an unknown outcome, as it does today.

### Exercise ordering in the existing harness

Replace the current same-thread reentrant contention checks in `tests/test_driver.c` with controlled concurrent requests: a synchronous waiting cut cannot finish from inside the callback that must release its device. Extend that harness to submit prints A/B, cut C1, print D, cut C2, and print E while A is paused at a controlled output point, then verify the specified execution order and each cut's delayed response. Verify a cut waits for an active status poll. Exercise the shared job-start guard with an accepted streaming-raster follower as well as the PNG scheduler path.

These assertions fail if contention is still refused, a later job overtakes a cut, a cut overtakes older accepted work, a response precedes execution, or polling blocks progress. Retain the existing exact-byte, no-op, transport-failure, authorization, and uncertain-output cases instead of duplicating them. Core-only tests cannot execute this PAPPL integration; run the existing adapter/container checks.

Update the README's cut behavior and the existing patch documentation with the queued ordering and synchronous wait. Document that a long or held earlier job can exceed the client's timeout; no deadline, asynchronous status endpoint, or timeout configuration is added.

## Risks / Trade-offs

- A client can time out while earlier work is pending. The server's synchronous request may still reach execution; the caller must treat the outcome as unknown and must not automatically replay it.
- Scheduling changes touch common PAPPL entrypoints. Keep the patch limited to acquisition ordering and run the existing ordinary print, raster, and both-model suites in the container. Existing physical release evidence establishes the unchanged cutter sequence, not new physical validation of this change.

## Cut pass

Removed repeated design statements about saved payloads/persistence and the existing uncertain-output guard: the current architecture and recovery specs already require them. No additional delta, failure suite, or hardware-validation task is needed for those unchanged behaviors. Unedited text and scenarios carried in the MODIFIED block are retained only because OpenSpec replaces that whole block.

| Added or revised item | Requirement served and why a smaller version fails |
| --- | --- |
| Spec sentence: place cuts between earlier and later accepted work | Issue #65's queued cut; waiting for any idle gap lets later jobs overtake the cut. |
| Spec sentence: wait through contention, including polling | Issue #65's observed busy refusal; retaining immediate refusal reproduces it. |
| Revised scenario: Printing or maintenance owns the device | Demonstrates older prints and interleaved cuts/new prints in one sequence; a lone idle cut cannot expose ordering. |
| Added scenario: Status polling owns the device | Existing ownership path also covers status; print-only coverage cannot establish waiting on this owner. |
| Queued PAPPL acquisition entrypoint and cut-handler call | The ordering clause and existing public-API boundary; the current nonblocking entrypoint refuses contention. |
| Waiter's job-ID boundary | The ordering clause; FIFO cuts alone cannot distinguish earlier prints from later ones. |
| Waiter's FIFO link | The ordering clause; one pending flag cannot retain interleaved cuts' distinct positions. |
| Printer-owned waiter head | The ordering clause; job start and acquisition need the same pending boundary. |
| Shared printer lock around admission and ownership handoff | Existing exclusive ownership; separate check/acquire steps race job scheduling or completion cleanup. |
| Job-start gate, including streaming raster | The ordering clause; streaming enters start_job without the normal queued-job check. |
| Poll acquisition defers to an eligible cut | The contention clause; ordinary nonblocking polling can repeatedly take the released device first. |
| Request-thread wait/recheck | Existing synchronous cut result; immediate return cannot report physical completion and another worker is unnecessary. |
| Remove waiter on acquisition/exit and resume scheduling | The ordering clause; an abandoned boundary prevents later jobs from starting. |
| Shutdown/deletion cleanup of waiting records | Request-owned record lifetime; destroying its owning printer before the wait exits invalidates the queued acquisition. |
| Coordinated print/cut regression with delayed-response assertions | Revised ordering scenario; old reentrant checks expect busy and cannot wait for their own callback to return. |
| Streaming-raster variant in the same harness | The ordering clause and observed direct entrypoint; PNG-only checks miss a concrete bypass in the pinned source. |
| Poll-owner regression in the same harness | Polling scenario; the old nested request would deadlock once waiting replaces refusal. |
| README cut instructions | Existing public operation documentation; its current immediate-busy/retry advice contradicts queued cuts. |
| Patch documentation | Existing Bounded dependency corrections requirement; a scheduling correction without its scope/removal condition violates that contract. |
| Task 1.1 | Ordering and contention clauses; a patch without adapter integration and concurrent assertions leaves the reported path unestablished. |
| Task 1.2 | Existing operation and dependency documentation; implementation alone leaves contradictory client instructions. |
| Task 2.1 | Repository core, adapter, both-model smoke, and OpenSpec gates; core-only checks cannot exercise the changed PAPPL scheduler. |
