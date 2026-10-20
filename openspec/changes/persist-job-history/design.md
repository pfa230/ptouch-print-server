# Design

## Context

See `proposal.md` for the problem. The live Pi container has no mounts; `deploy/pi/user-data` removes it at service start and uses `docker run --rm`. Journald is already on `/data/journal`, so changing logging cannot restore the web job list.

`src/main.c:system_cb` creates `ptouch` before returning to PAPPL. Pinned PAPPL 1.4.11's `mainloop-subcommands.c` then installs its default save callback and calls `papplSystemLoadState`. `system-loadsave.c` explicitly drops a saved printer and its jobs when creation finds that printer already exists. The same file saves identifiers, state/reasons, times and impression counts but not the `message` rendered by our existing job-history patch. Its default unfinished-job restoration can add surviving documents to `active_jobs`.

Existing `job-history` clauses already define the displayed fields, 20-row landing page, and failure explanations. Existing architecture clauses retain one configured queue, PAPPL scheduling, and conservative output recovery. This change adds durability, not another history interface.

## Goals / Non-Goals

**Goals:** Use one PAPPL state file and the existing retention machinery, loading once before creating a missing queue; make the supplied deployment retain that state independently of container lifetime.

**Non-Goals:** A second database or spooler, journal import, completed-document archival, persisted hardware continuation, automatic print resumption, a new retention setting, or power-loss guarantees beyond PAPPL's last successful state save. Changing the running Pi is a later deployment action.

## Decisions

### 1. Honor the existing spool-directory option

Read PAPPL's existing `-o spool-directory=...` in `system_cb`, pass it to `papplSystemCreate`, and use `<directory>/ptouch-app.state` for the application's save/load callback. Keep the callback's filename alive for the server lifetime. Without that option, retain the existing temporary spool behavior and use a per-user state filename under `papplGetTempDir`; only a supplied persistent directory promises deployment durability. An explicitly selected directory must not silently fall back to temporary storage.

The image entrypoint will create `/var/lib/ptouch` and pass `-o spool-directory=/var/lib/ptouch`. The Pi service will bind `/data/ptouch/state` there, creating that directory on the existing data partition; the USB Compose and manual Bluetooth examples will mount a persistent directory at the same destination. Reuse the existing service/update mechanism.

A mounted spool also temporarily retains unfinished document and attribute files. Keep PAPPL's existing disabled completed-document retention; the restoration path below removes interrupted-job files instead of printing them. A separate metadata-only volume plus temporary document spool would need another path mechanism without improving the requested history behavior.

### 2. Take ownership of save/load order through PAPPL APIs

After registering the transport, driver, operation callback, and PNG filter, register `papplSystemSetSaveCallback` and load the selected file before creating a queue. A registered save callback suppresses the mainloop's later automatic load. Reuse the restored `ptouch` queue; create it when the loaded state has no `ptouch` queue. Keep the existing configuration authoritative: a saved queue that conflicts with the configured supported model or resolved transport URI must fail startup clearly rather than select a different device. Do not duplicate queue-creation paths for restoration.

Using only a volume leaves the duplicate-printer failure. Loading after creating the queue has the same failure. Replacing the mainloop or adding a history parser is unnecessary because PAPPL exposes save/load and already permits a developer-supplied callback.

**Resolved review decision:** If configuration names a different model or transport URI, keep the existing choice to fail startup and preserve the saved history. This follows this section's explicit conflict policy and the requirement to preserve saved history rather than silently discard it; retargeting would add a public API beyond this design. Do not retarget or automatically delete saved history. For an intentional printer change, the operator stops the service, moves the old `/data/ptouch/state` directory aside as an archive, creates a fresh directory at that path, then configures/pairs and starts the service again. Document the equivalent fresh-directory procedure for other deployments. The Pi pairing helper must describe pairing/configuration success accurately and point to this recovery when changing printers, rather than claim the server now prints regardless of its startup outcome. Preserve an archive until the operator chooses to remove it; no automatic archive manager or extra option is needed. Verify the mismatch leaves the saved state file and its job history intact. Interrupted input payloads can already have been removed by restoration before the identity check; they are not part of that preservation assertion.

### 3. Extend the existing bounded job-history correction

Extend `pappl-1.4.11-job-history.patch` to round-trip an optional message in PAPPL's existing Job record, using its existing option escaping and a literal format when restoring message text. The field is optional because current jobs such as canceled jobs can have no message; their existing reason-keyword fallback remains useful. Keep job numbers, next-job numbering, counts and retention under PAPPL's existing state machinery. Include the pre-existing loader correction for Job records without `format`: PAPPL saves jobs canceled before receiving data and unfinished jobs awaiting data without this field, but its loader currently logs "Bad Job definition" and drops them. Accept an absent document format so canceled jobs retain their terminal history and unfinished jobs reach the normalization below.

Normalize every restored nonterminal job, including stopped jobs, to aborted completed history before it can enter `active_jobs`. Set an interruption/resubmission message, appending any saved diagnostic, set completion metadata, and remove its no-longer-printable spool files through existing cleanup operations. Do not restore driver run state or issue device commands. Update `patches/README.md` with this scope and removal criteria; both Dockerfiles already apply the same patch.

**Resolved review decision:** Normalize all nonterminal jobs loaded by the bundled PAPPL, without a driver-name restriction or a generic-driver restoration regression. The application registers only `brother_ptouch`; no existing fixture consumes native nonterminal restoration. AGENTS.md's minimality rule therefore selects this smaller correction rather than adding a restriction and test for an unshown consumer.

Leaving the native loader unchanged can replay a surviving document. Relying on missing spool files also fails: missing job attributes can leave a nonterminal record, and persisted files can enter the active queue. Calling the public cancel API after restoring a processing job leaves cancellation waiting for a worker that no longer exists. Normalizing inside the existing loader avoids another public mode or API.

### 4. Verify the real startup path using the existing image smoke

Extend `scripts/smoke-image.py` to mount one temporary host directory and replace its test container against the same directory. Reuse its socket sink, IPP client, successful job, malformed-PNG failure, canceled job and web-row checks. Assert the same job IDs, fields, messages, and exactly one configured queue after recreation. Recreate the container with a conflicting configured printer identity against the same directory; verify startup fails without changing the saved state file or its job history.

Add focused pinned-library coverage for restoring an unfinished job with its document and attributes still present: it becomes aborted history, has no active entry, and emits no device output. Reuse the state-roundtrip fixture pattern in `tests/test_pappl_finishings.c`; cover a stopped saved state as well as a processing one because the native loader branches at STOPPED. This complements the actual app/container test, which catches the startup-order failure a loader-only test misses. The image smoke's existing job canceled before receiving data covers both absent optional message and absent `format`; assert that it survives recreation as canceled history. Extend the focused state fixtures with an unfinished job awaiting data whose saved record has no `format`; verify it restores as aborted history with the interruption/resubmission message, no active entry, and no device output. No separate legacy fixture is needed.

Run the normal full CMake/CTest build with the pinned patched dependencies and the deploy-image smoke for both supported models. Core-only tests do not exercise startup or state restoration. A Pi reboot check can establish the real bind mount's persistence without printing; record it separately when a live deployment is authorized.

## Risks / Trade-offs

- **Last unsaved changes during power loss:** durability covers the last successful PAPPL save, not a new synchronous audit log; do not promise every job survives abrupt power removal.
- **Saved unfinished work:** it returns as aborted history requiring explicit resubmission; preserving documents must not silently enable resumption.
- **Existing hardware run after restart:** history restoration does not establish printer readiness or recover a held/uncertain run; keep the existing recovery procedure.

## Migration Plan

Update the image, service template and documentation together. For an existing Pi, document creating `/data/ptouch/state` and adding the bind mount to the service through the read-only-root maintenance procedure; changing the image alone cannot modify the installed systemd unit. Inspect/finish or recover any held print run before restarting. New cards receive the mount from `user-data`; the existing updater continues to select images and restart the service.

Use an existing state file only if it is actually available before container removal; previously deleted containers' job history has no supported backfill. Rollback keeps the host directory and mount; the old image may ignore the new history path and must not be described as preserving history. Diagnostic journal storage stays as configured.

## Cut pass

Removed from the candidate approach:

- A separate metadata-only directory and temporary-spool path: one mounted existing spool directory serves history durability and existing cleanup removes documents.
- A new state-directory environment option: the existing spool-directory option selects the needed path.
- A new public history-only loader mode and driver-name restriction: the bundled loader has no concrete native-resumption consumer to protect.
- A legacy-record fixture and generic-driver restoration regression: the canceled-job smoke covers absent messages, and no existing fixture relies on native nonterminal restoration.
- A separate startup/history test harness: the image smoke already starts the real app, submits representative jobs, captures output and reads web rows.

- An advancing-new-job-ID smoke assertion: pinned PAPPL already saves and restores `NextJobId`; no observed failure justifies an additional check.
- A zero-replay image-smoke assertion: the smoke recreates the container only after terminal jobs; the unfinished-job fixtures already exercise the native active-job restoration path and assert no device output.
- A terminal-document-payload smoke assertion: completed-document retention is already disabled, with no demonstrated failure requiring another check.
- A quoted-text/percent-sign message fixture: no concrete failure justifies these cases; the image smoke already checks exact application failure messages through IPP and web history after recreation. Literal-format restoration remains ordinary API use.
- A fresh-storage startup assertion and the printer-change scenario's fresh-start result: empty storage falls outside the saved-identity-conflict condition, and both model image smokes already exercise first startup. The conflict explanation still names fresh-directory recovery and preserves saved state.
- The interrupted-payload parenthetical in task 2.2: that smoke recreates only after terminal jobs; Decision 2 retains the preservation exclusion for interrupted payloads.
- Expected build-integration changes in the proposal impact: both Dockerfiles already apply the patch; task 1.2 retains verification of those existing steps.

Kept items, each with its contract and the failure of the smaller alternative:

- History durability sentence: new `Saved job history survives restart`; process-only records disappear on container replacement.
- Saved metadata sentence: the same requirement; retaining only IDs loses the status and explanations the user needs.
- Persistent deployment sentence: the same requirement; a working serializer still loses its file with an unmounted container.
- Container-replacement scenario: the same requirement; a library-only roundtrip misses both the missing mount and duplicate-queue startup.
- Interrupted-history sentence: new `Interrupted jobs return as history`; retaining a pending/processing state can reactivate saved input.
- Unfinished-input scenario: the same requirement; omitting surviving inputs would miss the loader's active-job branch.
- Saved-history identity sentence: new `Saved history belongs to the configured printer`, completing the already chosen fail-on-conflict policy; silent deletion loses the requested history and retargeting adds an unnecessary API to this design.
- Printer-change scenario: the same requirement; ordinary same-printer recreation never exercises the pairing helper's address change.
- Existing recovery prohibition sentence: carried unchanged under MODIFIED as required by the delta format; removal would weaken the existing no-replay contract.
- Existing idempotent-cleanup sentence: carried unchanged; removal would weaken abort cleanup.
- Existing explicit-recovery sentence: carried unchanged; removal would authorize recovery without inspection and resubmission.
- Recovery qualification sentence: architecture `Conservative output recovery`; retaining the queue/history disclaimer would contradict the new durability requirement.
- Existing incomplete-label scenario: carried unchanged with its requirement; removing it would drop the existing interrupted-output case.
- Spool-directory option handling: history durability; ignoring the already available option leaves the mounted path unused.
- Persistent directory and Pi bind mount: persistent deployment sentence; no mount repeats the observed loss.
- Temporary per-user default: existing runnable application outside the supplied container; an unconditional root-owned deployment path breaks nonroot invocation.
- State filename retained for callback lifetime: PAPPL save API stores callback data; a callback-local filename would expire before saving.
- Save callback registration before load: history durability; leaving registration to the mainloop repeats its late load.
- Load before fallback queue creation: history durability and existing single-queue contract; create-first drops the saved printer's history.
- Restored identity validation: existing supported/configured-device contract; blindly trusting the saved URI can select the old Bluetooth address instead of current configuration.
- Manual fresh-directory recovery and accurate pairing-helper output: saved-history identity requirement and existing intentional printer configuration; failure without this recovery leaves re-pairing stuck, while automatic archive management adds machinery a person can avoid by choosing fresh storage.
- Identity-conflict check: saved-history identity requirement; same-printer recreation cannot establish that a mismatched configuration fails startup and preserves saved history.
- Existing PAPPL state record: history durability; a second database duplicates serialization and retention.
- Existing job-number field: saved metadata; losing it breaks references to earlier jobs.
- Existing next-job-number field: saved metadata and native PAPPL job identity; restarting at one collides with restored IDs.
- Existing job-name field: saved metadata; omitting it hides which document failed.
- Existing owner field: saved metadata; omitting it loses the submitted identity already shown by the UI.
- Existing timestamp fields: saved metadata; omitting them loses ordering and failure time.
- Existing page-count fields: saved metadata; omitting them loses recorded progress.
- Existing terminal-state field: saved metadata; omitting it loses completed/aborted/canceled distinctions.
- Existing reason-bit field: saved metadata and existing failure-reason fallback; omitting it leaves old/message-free records unexplained.
- Optional document-format loading: saved canceled history and interrupted history; the existing loader rejects records without `format` as "Bad Job definition" and drops jobs canceled before receiving data or still awaiting data.
- Optional message field: saved metadata and existing `Failed jobs show their reason`; current PAPPL serialization demonstrably omits the displayed text.
- Existing option escaping and literal message restoration: saved metadata through PAPPL's existing option and message APIs; raw token splitting loses spaces in current failure messages, and the literal format passes restored text as data.
- Nonterminal normalization: interrupted-history requirement and existing no-replay rule; native restoration can reactivate surviving documents, while a driver-name restriction has no concrete consumer to protect.
- Interruption message/completion metadata: interrupted-history requirement; changing state alone does not explain why the job stopped.
- Input-file cleanup with existing disabled completed-document retention: existing PAPPL behavior and proposal boundary against archival; mounting the spool otherwise leaves interrupted payloads behind.
- Existing retention limit: bounded history requirement; a second limit would add an unnecessary setting and differing eviction policy.
- Canceled-before-data smoke assertion: saved terminal history; checking only jobs with documents misses the observed rejection of a saved canceled job without `format`.
- Awaiting-data restoration fixture: interrupted-history requirement; processing/stopped fixtures with present inputs do not exercise the loader's rejection before nonterminal normalization when `format` is absent.
- Processing/stopped restoration fixtures with present inputs: interrupted-history requirement; the native loader treats STOPPED differently from earlier states.
- Real-app image replacement smoke: persistent history scenario; isolated save/load cannot expose the app's create-before-load defect.
- One-queue assertion in that smoke: existing single-queue contract; the app currently creates the queue before loading saved printers, causing the duplicate-printer failure.
- IPP/web message assertions in that smoke: existing visible-history clauses plus durability; file contents alone do not establish that users see the restored explanation.
- No-device-output assertion in focused unfinished-job fixtures: interrupted-history requirement and existing conservative recovery; the native loader can place surviving inputs in `active_jobs`, so visible aborted history alone does not establish that restoration never dispatched its input.
- Full build/CTest and both model image smokes: project-required adapter verification; core-only checks never execute application startup or pinned PAPPL state code.
- New-card service/example changes: persistent deployment sentence; updating only the app still leaves an unmounted state file.
- Installed-Pi migration documentation: explicit requested Pi fix; a cloud-init template update never changes the already installed systemd unit.
- Patch scope/removal documentation: existing bounded dependency-correction requirement; undocumented new loader behavior cannot be safely reevaluated on a dependency update.
- Last-save/backfill/rollback explanation: proposal's deployment documentation request; promising recovery of already deleted or unsaved records would be false.
- Task 1.1: saved metadata and interrupted history; omitting it retains the loader's missing-message/replay behavior and rejection of jobs without `format`.
- Task 1.2: bounded dependency corrections; omitting it leaves scope/application unverified.
- Task 2.1: history durability; omitting it retains duplicate-printer loss.
- Task 2.2: visible durable history; omitting it leaves actual container recreation unverified.
- Task 3.1: persistent deployment sentence; omitting it reproduces the live Pi's missing mount.
- Task 3.2: user-requested existing-Pi fix; omitting it leaves existing installations without an upgrade path.
- Task 4.1: project-required integration verification; narrower core tests do not exercise the changed system.

No new scheduler, database, retention knob, public API, journal mechanism, or device-run state field remains in the plan.
