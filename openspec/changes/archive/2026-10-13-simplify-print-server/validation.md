# Current validation status

[verified] All 42 tasks are complete. The three specifications are synced and
strict validation passes. The change was archived locally on 2026-10-13.

The authorized 21-label integrated PT-2730 acceptance passed on 12 mm tape,
including idle/offline continuation, cassette change, truncated-input blocking,
and explicit recovery without replay. The earlier eight-label direct-protocol
24 mm gate also passed. Other supported widths retain software-only coverage.
The original deployed service was restored on its unchanged image. No further
printing or deployment is authorized.

Claude's independent implementation review is retained in
[evidence/claude-implementation-review.txt](evidence/claude-implementation-review.txt).
The physical session's requests, fixtures, logs, USB capture, and status packets
are preserved in [evidence/ptouch-integrated-12mm.tar.gz](evidence/ptouch-integrated-12mm.tar.gz),
without compiled tools or the candidate container image. Entries below record
historical checkpoints; their pending-task language describes that checkpoint,
not the current physical acceptance status. Final review fixes and their fresh
software verification are recorded at the end before archival.

# Validation history

Work remains on `refactor/simplify-print-server`. No commits were pushed and
the deployed image has not been replaced. Initial software validation had no USB
access; the subsequently approved eight-label gate preparation is recorded below.

## Verified foundation

- Explicit core-only builds run six CTest suites without PAPPL discovery.
  Full builds require PAPPL, libusb, libpng, and Python; missing dependencies
  fail configuration instead of silently omitting the app or capture tests.
- Both PR and main CI run the deploy build stage before publication. A temporary
  deliberately failing CTest stopped the build before application installation
  and runtime-image creation.
- Fake USB enumeration covers zero, one, and two matching devices, unsupported
  devices, changing USB nodes, and claim failures. Configuration and driver
  registration reject unknown or unsupported model identities.
- Real PAPPL socket connections and injected Brother presence exercise the
  status transport guard without USB operations or opaque-data access.
- A real isolated server completed a PNG job and aborted a malformed PNG job,
  with startup, job, completion, and error messages on stderr. Injected dbus and
  Avahi startup failures stopped the entrypoint before starting the application.

Checks ran in local containers with repository source mounted read-only, no USB
mount, and no external network during the test runs. The host has no CMake;
container builds provide both core and adapter coverage. Build/CI, device
selection, and logging changes received a read-only review; its probe URI
assignment finding was fixed and both build modes passed afterward.

## Raster boundary correction

The PAPPL patch and its independent IPP/socket fixture cover submitted geometry,
conversion, malformed headers, media conflicts, incomplete rows, and callback
failure cleanup. This fixture uses a minimal driver to isolate the dependency
boundary; it does not certify the Brother finishing sequence.

The initial checkpoint passed nine CTest suites with 20 real IPP raster/image
cases. Expanded coverage now includes malformed stream termination as required
coverage. Patch application succeeds on pinned source and fails for duplicate
application or corrupted context. Tasks 2.1, 2.2, 2.3, and 2.5 are complete;
task 2.6 stays open until the finishing bridge is also documented.

See [patch documentation](../../../../patches/README.md) for pinned dependencies,
security correction policy, scope, and removal conditions.

## Approved scope extension

The original regression appended one byte after a complete URF label and still
produced a completed job and successful end-job callback. CUPS 2.4.x reported a
short next header like clean EOF; its private compressed-input buffer prevented
a reliable PAPPL-side byte-count check.

The user approved a bounded CUPS decoder correction that reports
partial-header failure through its existing error mechanism, plus PAPPL error
propagation before normal end-job. The proposal, design, and tasks include that
extension beyond PAPPL-only dependency patches. Both development and deploy
images now build Debian's security-patched CUPS source through the shared helper;
runtime checks show PAPPL and libcupsimage resolve the patched libcups. Required
regressions also cover negative input callbacks for PWG, URF, and uncompressed
raster. Task 2.4 is complete: the rebuilt deploy image and optimized Release
build pass all ten suites, and the final runtime IPP smoke and library/source
version checks pass. Captured Release flags retain -UNDEBUG after -DNDEBUG.

## Geometry, PNG, and printer ownership

- All five tape widths share canonical nominal/imageable geometry. Publication
  and placement regressions cover their pixel windows, including 18 mm margins,
  wrong tape, and unsafe requested margins before page output.
- Real IPP PNG/JPEG jobs cover explicit 50 mm media and PNG-only auto-length,
  named media and media collections, nominal blank margins, oversized content,
  orientation markers, DPI, length boundaries, copies, and standard scaling.
  PNG is routed as image/png with no private advertised format. Malformed image
  and transfer failures emit no subsequent raster or finishing command.
- Printer-owned extension data replaces the eight-slot global registry. Fake
  status and transport tests cover cassette changes, unavailable/error states,
  failed output confirmation, and healthy rejected/canceled followers. Clean
  status or USB reopen cannot clear uncertain output; recovery is documented as
  printer power-cycle followed by server restart. Physical recovery is pending.

A read-only review of the completed state/status/geometry and PNG source plus
behavioral assertions found no actionable regressions. Pending finishing work
was explicitly excluded from that conclusion.

## Test fixture correction

An attempted HTTP connection shortcut to wake PAPPL at shutdown introduced a
detached-client lifetime hazard: joining the system thread does not join that
client before system deletion. The shortcut was removed from all three fixtures.
Tests retain PAPPL's ordinary shutdown polling. Test-only -UNDEBUG keeps assertion
checks and fixture setup active in Release; production compiler flags are
unchanged. All six core-only suites pass in Release with assertions enabled.
Final full image verification passes. Five repeated driver runs and five repeated
PNG runs also pass in Release with assertions enabled, taking 299.91 seconds;
`ctest-repeat-fixed.log` records this additional teardown check.

One earlier default-build crash has no retained compiler flags or backtrace, so
its exact cause is unproven. Separately reproduced failures establish disabled
Release fixture assertions and intermittent heap corruption with the shutdown
shortcut. No useful sanitizer stack trace was obtained.

At this earlier checkpoint, local evidence was retained under
`/tmp/ptouch-pappl-raster-results/`, `/tmp/ptouch-cups-raster-results/`, and
`/tmp/ptouch-driver-crash-results/`.
The final dependency/build evidence is `docker-deploy-build-final-verified.log`,
`release-ctest.log`, `release-driver-flags.make`, `release-raster-flags.make`, and
the `runtime-*-final.log` files in the CUPS results directory. Older failing logs
are retained as historical debugging evidence, not the final verification.

## Hardware gate

The test-only batch probe and 11 command-capture cases cover the proposed four
modes, first-label precut, deferred terminal command, and multi-batch transitions.
The user approved a reduced initial gate of eight labels on 24 mm tape:
chain:2 followed by each:2, and none:2 followed by end:2. Framed raster totals
223.52 mm, plus two initial leaders and feed margins. The earlier 27-label
proposal is superseded; the larger integrated acceptance matrix is separate. See
[probe instructions](../../../../tests/CUT-PROBE.md) for exact commands, expected
cuts, continuation checks, and failure stop conditions.

The four-preset driver lifecycle and PT-2730 hardware gates remain pending.
Command captures and socket jobs do not establish physical cuts or leader waste.

### Eight-label gate preparation

[verified] On vault, the existing ptouch queue was idle with zero jobs. Its
server was temporarily stopped for a direct status-only query, then restored.
The query emitted no reset, raster, feed, or cut. The reply was type 00, phase 00,
error 0000, with cassette width 0 mm:
`8020423063300000000000000000000000000000000000000000000000000000`.
No physical labels were sent. Load/confirm the 24 mm cassette and power-cycle
before continuing. The probe, status log, and future captures are in
`vault:/tmp/ptouch-eight-label-gate.MDnnuO/`.

The exact eight-label policy was separately captured without USB: both pairs
contain four labels, one initialization, one EJECT, no empty terminal command,
and the expected first/later autocut flags. The current compatible runtime
executes the transferred probe successfully.

### Initial gate, stage 1: two chain-only labels

[verified] After the operator reseated the cassette and power-cycled, a direct
status query reported clean 24 mm tape:
`8020423063300000000018010000000000000000000000000000000000000000`.
The original server was then stopped for the approved physical test. The probe
transferred exactly two chain-only labels (7,564 bytes); each received the
printing/completion/receiving-ready sequence with zero error bits. Captures show
180/216 rows, two FF commands, one initialization, and no EJECT.

Evidence: `vault:/tmp/ptouch-eight-label-gate.MDnnuO/chain-2.{prn,log}` and
`status-after-power-cycle.log`. The server remains temporarily stopped to avoid
resetting the healthy held run during physical inspection. Two of eight labels
have been sent; leader count, cut separation, and held end await operator
confirmation. Task 5.1 remains unchecked, and the production finishing path is
unchanged.

The operator reported `leader-cut-rectangle-cut-rectangle`, matching the
expected initial leader and cut between the two labels, with no trailing cut
reported. The following chain-and-final batch is being tested as continuation
of this same held run.

### Initial gate, stage 2: two chain-and-final labels

[verified] The continuation probe transferred 7,566 bytes and received clean
completion/receiving-ready notifications for both labels. Its capture contains
180/216 rows, intermediate FF, late chain-off and final EJECT, and no ESC @
initialization. Evidence: `vault:/tmp/ptouch-eight-label-gate.MDnnuO/each-2.{prn,log}`.

The operator reported `leader-cut-label-cut-label-cut`, then clarified that
"leader" meant the last rectangle held from the first batch, not blank scrap.
This confirms the prior held label was released, both new labels were cut,
the final batch cut succeeded, and no inter-batch leader was discarded. Four
of eight labels have been sent. The original server was restored after the
confirmed final cut. Both zero-error receiving-ready and the operator's final
cut confirmation establish a closed run for the next fresh initialization;
another manual power-cycle is unnecessary here. Task 5.1 remains unchecked
until the no-cut/final-only pair is physically verified.

### Initial gate, stage 3: two no-cut labels

[verified] The idle restored queue had zero jobs; a direct status query confirmed
clean 24 mm tape before the server yielded USB access. The no-cut probe then
transferred 7,564 bytes and received clean completion/receiving-ready for both
labels. Captures contain 180/216 rows, first-label autocut on, later-label autocut
off, two FF commands, and no EJECT. A fresh initialization followed the preceding
physically confirmed final cut, rather than resetting an unfinished run.

Evidence: `vault:/tmp/ptouch-eight-label-gate.MDnnuO/none-2.{prn,log}` and
`status-before-none.log`. Six of eight labels have been sent. The original server
remains temporarily stopped during inspection of the held strip; physical
confirmation of one leader and an uncut two-label strip is pending.

The operator reported `leader-label`, with the second rectangle potentially
inside the print head. No intermediate cut was reported; the joined strip will
be inspected fully when the following batch's precut releases it.

### Initial gate, stage 4: two final-only labels

[verified] The continuation probe transferred 7,566 bytes, with clean completion
and receiving-ready for both labels. Captures show 180/216 rows, first-label
autocut on and later-label autocut off, intermediate FF, late chain-off and final
EJECT, and no ESC @ initialization. Evidence:
`vault:/tmp/ptouch-eight-label-gate.MDnnuO/end-2.{prn,log}`.

The operator reported `finished double label from last turn-cut-double label-cut`.
This confirms the no-cut batch was joined, its held end was released by the next
batch, the final-only batch was joined and cut free, and no new blank scrap was
reported between them. All eight authorized labels have been sent. The original
server is running again; its image/configuration was not replaced.

The operator subsequently confirmed the no-cut batch's fresh-run blank leader
was cut off. The eight-label initial gate passes all four cutting policies,
mandatory first-label precut including none, late chain-off/final EJECT, and
healthy continued batches without discarded leaders. Task 5.1 is complete.
This evidence covers these bounded two-label direct-protocol cases on 24 mm
tape; the integrated PNG/raster, media/recovery, and other-width acceptance
matrix is still pending. Actual tape length was not measured. No further labels
are authorized by this completed eight-label gate.

## Completed batch implementation

The Release driver matrix passes against the initial finishing SDK with test
assertions enabled. Tasks 5.2, 5.5–5.9 are complete: real IPP all-mode PNG1/3,
unknown-count mixed-length PWG1/8 and URF8, enum/template/no-value selections,
16 run transitions, deferred output and failure cleanup, mandatory first precut,
PNG65/999 counting sinks, and truthful raster copies=1. The retired precut/width
settings have no production read or replacement knob; task 6.2 is complete.

Evidence: `/tmp/ptouch-driver-production-results/task-evidence.md` and
`driver-matrix-pass.log`, plus the pure cutter/core/capture results. The previous
full-suite log includes a corrected raster-copies test expectation and a pending
PAPPL public-default setter regression; it is not the final all-suite result.

An ordinary driverless CUPS queue exposed a separate media-advertisement gap:
it submits 12x50 mm with the printer-wide 53/53 minimum margins, exceeding that
tape's physical window. The driver correctly rejects it without page output.
Task 7.2 remains open while canonical per-size media metadata is corrected and
the conventional queue rerun. The synchronized default/persistence bridge,
deploy-image smoke, final all-suite checks, and integrated physical acceptance
remain open.

## Final finishing and conventional CUPS checks

[verified] Tasks 5.3/5.4 pass the standalone real-IPP PAPPL fixture and Release
production driver matrix. Defaults synchronize across enum/collection updates,
the public setter, and saved-state reload; malformed/contradictory defaults leave
the previous value unchanged. Legacy fixture behavior is retained. The refreshed
SDK has canonical media overrides with one 20-entry database and five fixed-width
variable-length ranges. The driver/PNG suites pass 2/2, including partial second
labels in all four modes without terminal EJECT. Evidence:
`/tmp/ptouch-driver-production-results/ctest-final.log` and
`/tmp/ptouch-final-deploy-build.log` (PAPPL fixture passes; the first full run's
media test omitted the explicit database request and was corrected).

[verified] Task 7.2 passes against a conventional driverless CUPS queue in an
isolated, network-disabled container without USB. `lp` submits PNG, JPEG, and
two-label PDFs at 12x50 mm with captured 354-row labels. Enum 60 separates labels
and cuts the last; enum 63 leaves the PDF labels joined and cuts the last. Two
valid jobs remain queued while the printer socket refuses connections, then
both complete after the socket becomes available. A direct standard collection
request selects `ptouch-chain` and leaves the last label held. All-mode
one/three-copy PNG and unknown-count raster plus input/output failures are
covered by the production driver matrix. Evidence:
`/tmp/ptouch-cups-queue-offline.log`; reproducible fixture:
`scripts/check-cups.py`. Ordinary CUPS queue settings use
`lpoptions -p <queue> -o finishings=11` to override a generated/cached explicit
none default; server-side explicit none remains honored.

The final runtime/full-suite run and image smoke are still pending. A separate
Web UI GET truncation is under diagnosis; the new legacy-form guard preserves
canonical defaults in source but that form regression is not yet verified.

[verified] The final source/doc/spec review found two guide references and a fixture
retry mismatch; all are corrected. Task 7.4 is complete. The retired mechanisms
remain absent, the bounded patches are explained beside their source, and strict
OpenSpec validation passes both current items. The guide now describes metadata
lookup separately from the explicit supported-model gate.

[verified] The Web UI failure reproduces in the pre-finishing application too:
PAPPL's optional NULL footer is passed to localization and crashes the renderer.
The app now initializes an empty footer through the existing public API. The
reproducible CUPS fixture passes actual GET/POST Printing Defaults checks with
both enum 11 and enum 63 unchanged after an unrelated print-quality edit, followed
by all ordinary CUPS/offline/collection cases. Evidence:
`/tmp/ptouch-cups-web-final2.log`; baseline crash trace:
`/tmp/ptouch-driver-production-results/web-baseline-app.log`.

The final image smoke administers defaults through the existing container-local
`ptouch-app modify` CLI, which sends standard Set-Printer-Attributes; it preserves
PAPPL's existing restriction on unauthenticated remote administrative operations.

## Software completion

[verified] All authorized software tasks are complete, 39 of 42 overall. The final
runtime build includes the empty-footer fix and passes all 11 CTest suites in
120.19 seconds. The finished image smoke passes all four presets, explicit 50 mm
geometry, three-copy PNG batches, healthy continuation, synchronized omitted
printer defaults, and malformed-image abort without printer output. Evidence:
`/tmp/ptouch-final-deploy-build3.log` and `/tmp/ptouch-final-image-smoke2.log`.
The durable conventional CUPS/Web fixture passes too. No extra physical labels
were printed during these software checks.

Tasks 3.7 and 7.3 require a separately authorized integrated physical acceptance
and uncertain-output recovery session; the completed eight-label approval does
not authorize further printing. Task 7.5 remains pending until those requirements
are fulfilled and the change can be synced/archived. No image has been deployed,
no commits/branches pushed, and no PR opened. Strict OpenSpec validation currently
passes; this is not archival or a waiver of the physical checks.

The remaining physical session is concretely bounded in
`tests/HARDWARE-ACCEPTANCE.md`: 20 integrated acceptance labels plus one recovery
label, with an incomplete upload and blocked attempt issuing no print/feed command.
Printed content is about 54 cm before unmeasured leaders/feed. This is prepared only,
not authorized or run; it adds no physical evidence to the completed eight-label
gate. Approval must precede printer-host setup or additional output.

## Integrated physical session authorization

The operator answered "12mm in, ready" to the proposed bounded 21-label session.
This authorizes the documented matrix and explicit recovery, starting with 12 mm
tape, not permanent deployment or unbounded repeats. The existing `vault` service
is running the original image `sha256:8f0de486c933c9eca20a772be9cd647bf377b53114c8d20cdb5805336d2a8ec0`;
its standard `jobs` CLI reports no jobs, and `status` reports idle. Evidence will
be retained at `vault:/tmp/ptouch-integrated-acceptance.Nj73Wz/`.
No labels have yet been sent in this session.

### Integrated stage 1: three chain-only PNG copies on 12 mm

[verified] The candidate preflight reports clean 12 mm tape, canonical 63/71 margins,
and no jobs. One normal IPP PNG job with three copies completed successfully.
Transparent test-only USB recording contains 180/180/180 raster rows, mandatory
first precut and between-label autocut, one initialization, three FF commands,
zero EJECT, and clean completion/receiving-ready after each label. Three of 21
complete labels have been sent. Physical separation, leader, and held-end
observations are pending. The original service remains temporarily stopped and
the isolated candidate remains running to preserve the healthy held run.
Evidence: `vault:/tmp/ptouch-integrated-acceptance.Nj73Wz/stage 1.log`,
`usb-io.jsonl`, and local `/tmp/ptouch-integrated-first/stage 1.print.prn`.

The operator confirms stage 1: "leader and 2 labels with rectangles cut off, last
one held". This matches the mandatory fresh-run precut, two between-label cuts,
and the held final label. Stage1 passes physically; three of 21 labels are complete.
The candidate remains running across this idle gap. All remaining 12 mm fixtures
were independently verified through isolated IPP/socket captures before hardware
use, including exact PNG/PWG pixels and incomplete input after 60 rows with no
FF/EJECT and a blocked following job. No additional physical labels were sent.

### Integrated offline queue check

[verified] After the operator disconnected USB, job 2 (two chain-only PNG copies)
was accepted and waits for the device with zero impressions. PAPPL reports this
active waiting job as processing, so the fixture's initial pending-only assertion
was corrected using a read-only query; the job was not replayed. Job 3 (one
chain-and-final PNG) was accepted held for the next inspection. Captured USB
output remains exactly unchanged at 10326 bytes. Two valid jobs are queued while
the printer is disconnected; physical resumption is pending. No labels beyond
the first three have been printed. Evidence: `stage 2-queue.log`,
`stage 2-offline-check.log`, and `stage 3-queue.log` in the session directory.

### Integrated stages 2/3: offline continuation and finishing

[verified] After USB reconnection, the queued two-label chain job completed with
180/180 rows, two FF commands, zero EJECT and no initialization. The operator
reports the prior held label cut free, then one new label cut free and the second
held, with no new blank leader. Releasing the existing held one-label finishing
job then produced 180 rows, late chain-off and EJECT, again without initialization.
The operator confirms the prior label was released and the new label cut free.
Both queued jobs resumed without replay or a discarded leader. The idle gap
between stage 1 and stage 2 was about an hour. Six of 21 labels are complete and
the run is physically confirmed closed. Captures and job completions are retained
in `stage 2-completed.log`, `stage 3-completed.log`, and `usb-out.bin`.

The operator confirms stage 3: prior held label released, one new label printed
and cut free. Both physically inspected offline/continued jobs pass. A concrete
24 mm media-col request while 12 mm is loaded then aborted with "Page geometry does
not match loaded 12 mm tape". Its capture contains only two status requests and
no initialization, raster, FF or EJECT. The fixture was not replayed. The candidate
remains healthy after the rejection; the next counted stage is the two-label
no-cut batch. Evidence: `wrong-tape.log` and its captured output boundary.

### Integrated stage 4: two no-cut PNG copies

[verified] The two-copy enum-none job completed with 180/180 rows, first-label
precut enabled and later autocut disabled, two FF commands, zero EJECT and one
fresh initialization after the preceding confirmed final cut. Eight of 21
complete labels have been sent. Physical joined-strip/leader observations are
pending. The candidate stays running with its final strip held.
Evidence: `stage 4.log`, `stage 4-offset`, and `usb-out.bin`.

The operator confirms stage 4: "leader cut off, then 2 labels, both held".
The joined two-label strip remains attached, matching no internal or terminal
cut. Stage 4 passes physically. The following no-cut batch is submitted as a
healthy continuation, with no server restart or reset.

### Integrated stage 5: second no-cut batch

[verified] The second two-copy enum-none job completed with 180/180 rows, first
precut enabled, later autocut disabled, two FF commands, zero EJECT, and no
initialization. Ten of 21 complete labels have been sent. The operator reports
"leader cut off, next 2 labels held"; clarification is pending whether "leader"
means the previous joined two-label strip, rather than new blank scrap.
The healthy run remains held; no following batch has yet been submitted.
Evidence: `stage 5.log`, `stage 5-offset`, and `usb-out.bin`.

The operator clarifies stage 5's "leader" was the previous 2-label strip.
This confirms separate joined strips across consecutive enum-none batches,
mandatory batch-boundary precut, and no discarded blank leader. Stage 5 passes
physically, with ten of 21 labels complete. The one-label final-only continuation
is now submitted to release the held strip and close the run.

### Integrated stage 6: final-only continuation

[verified] The one-label enum 63 job completed with 180 rows, late chain-off/EJECT,
mandatory first precut, and no initialization or empty terminal command. The
operator reports the prior two-label strip released and the new label printed
and cut free. The none/none/final-only sequence passes physically without a new
blank leader between healthy batches. Eleven of 21 complete labels are sent;
the run is confirmed closed. Evidence: `stage6.log` and its command capture.

### Integrated stage 7: three chain-and-final PNG copies

[verified] Three-copy enum 60 PNG job completed with 180/180/180 rows, mandatory
initial precut and internal cutting, FF/FF/late-chain-off-EJECT, one fresh
initialization, and no empty terminal command. The operator confirms one leader
and three labels cut free. Fourteen of 21 complete labels are sent; the run is
confirmed closed. Evidence: `stage7.log` and the stage 7 command boundary.

### Integrated stage 8: unknown-count three-label raster

[verified] One standard PWG job with optional total-page count 0 completed with
180/216/180 rows, FF/FF/late-chain-off-EJECT, one fresh initialization, and no
empty terminal command. The operator confirms three labels, all cut free.
Seventeen of 21 complete labels are sent; the run is confirmed closed. Physical
length/orientation marker details still require inspection. Evidence:
`stage8.log`, its output boundary, and the validated `raster-3.pwg` fixture.

### Integrated stage 9: three-copy final-only strip

[verified] The landscape PNG job completed three 180-row labels, FF/FF/late-chain-off
EJECT, first autocut enabled and later cuts disabled, one fresh initialization,
and no empty terminal command. The operator confirms one leader and a joined
three-label strip cut free at the end. Twenty of 21 complete labels are sent;
the run is confirmed closed. Evidence: `stage9.log` and its capture boundary.

The stage 8 raster test client timed out after 10 seconds because streaming
Print-Job responds after physical printing. No job was replayed. A read-only
Get-Job-Attributes for its existing job 9 confirms completed state 9 and three
impressions; server logs and the 576-row capture agree. Subsequent physical
streaming checks use a 45-second client timeout. This is a test-timeout correction,
not a relaxed completion/status requirement. Evidence: `stage8-confirm.log`.

### Integrated uncertain-output blocking

[verified] The authorized incomplete single-page PWG upload aborted after 60 raster
rows with no FF/EJECT, so no complete label was printed. A status-only probe on a
fresh USB connection reports clean 12 mm tape and error 0000/phase 00. The following
valid marked PNG still aborts with "Output uncertain: power-cycle the printer,
then restart the server before resubmitting", and its capture has zero setup,
raster, feed or cut. Clean status and USB reopen did not clear the latch. Twenty
of 21 complete labels remain printed; explicit power-cycle/restart and the single
marked recovery label are pending. Evidence: `incomplete.log`,
`status-while-uncertain.log`, `blocked.log`, and corresponding capture boundaries.

The operator replies "ready" after the explicit printer power-cycle instruction.
Only the isolated candidate server is being restarted; failed jobs are not
resubmitted automatically. The final marked recovery label is submitted explicitly
after clean 12 mm/no-jobs preflight. Physical absence of ghost output and final
marker/geometry inspection remain pending operator confirmation.

### Integrated stage 10: explicit recovery label

[verified] After the operator's power-cycle and the candidate-only restart,
preflight reports idle, zero jobs, clean 12 mm tape and canonical margins. The
explicitly resubmitted marked recovery label completes with 180 rows, one fresh
initialization and one late-chain-off EJECT. No additional raster image or print
command is emitted. All 21 authorized complete labels have now been sent; the
incomplete/blocked trials emitted no FF/EJECT and no complete label. Physical
absence of ghost output and final marker inspection are pending confirmation.
The original deployed image/configuration remains unchanged; its service is
still temporarily stopped until recovery/closed-run inspection is confirmed.
Evidence: `recovery-preflight.log`, `stage10.log`, and the final capture boundary.

The operator confirms the recovery output: "leader and one label cut off".
No additional fragment or replay is reported, matching the single 180-row capture
and completed recovery job. Physical power-cycle followed by server restart and
explicit resubmission passes. README recovery/job-inspection guidance and
ADR-0002/0007 were reviewed; README now supplies the concrete jobs/logs/status
commands and distinguishes failed-job inspection from queue persistence.

[verified] The temporary candidate was stopped and removed after this physically
confirmed final cut. The original `ptouch` service is restored, running, and idle
on the unchanged image `sha256:8f0de486c933c9eca20a772be9cd647bf377b53114c8d20cdb5805336d2a8ec0`.
Its queue has no jobs. All 21 complete labels and the bounded negative trials are
finished. Final physical geometry/orientation marker confirmation remains open
before task 7.3 and archival can close. No further printing is authorized.

The operator's final layout confirmation is "all good": complete rectangle
outlines, the longer middle raster label, and upright text on the joined strip.
All 21 physical labels and the bounded negative/recovery trials pass on 12 mm tape.
Combined with the earlier approved eight-label 24 mm protocol gate and five-width
software captures, this satisfies the recorded physical acceptance scope; no
hardware claims are made for 6/9/18 mm tape. The original service is restored.
The user subsequently requested Claude review of the implementation before final
sync/archive; no additional printing, publishing or deployment is authorized.


## Final implementation review fixes, 2026-10-13

[verified] Claude identified named-only media geometry, busy-status publication,
and stale documentation. Its complete review is retained under evidence/.
Named-only collection media now preserves a PNG's 50 mm page and rejects conflicting
raster dimensions before page output, matching PAPPL's size-name precedence.
The new raster reproduction failed on the previous SDK and passes 47 IPP jobs plus
three negative-I/O cases on the corrected one. Patch application passes with
fuzz 0, the finishing patch still applies afterward, and duplicate/corrupt-context
applications fail. The production PNG fixture proves 354 rows for 50 mm and zero
output for too-short or wrong-tape named media.

[verified] Idle status no longer publishes recovery/offline during active printing;
the NULL-open branch rechecks processing to distinguish scheduling from claim
failure. A real scheduled-job regression preserves media confirmation and reasons
without extra status reads. Existing idle unclaimable-USB rejection still passes.
The focused driver and PNG suites pass 2/2 in 29.51 s under Release with no warnings.

[verified] The missing ordinary CUPS cases are now included in scripts/check-cups.py.
Two copies of the two-label PDF produce four 354-row labels. CUPS's Custom.12x40mm
submits 1199 × 3986 hundredths of a millimeter and explicit 297/297 margins; it takes the maximum margins
across tapes and flattens per-width custom ranges into one PPD custom size.
The server now advertises only ±0.01 mm width tolerance around each nominal tape,
retains submitted dimensions and explicit margins, and keeps the physical window
unchanged. All five widths accept ±0.01 mm and reject ±0.02 mm with fidelity enabled. A fitting
42-dot custom PNG produces 282 rows; the original 76-dot PNG fails with the correct
42-dot limit and zero output. Exact standard media-col 1200 × 4000 with the actual
63/71 tape margins prints the 76-dot design in 283 rows. No hidden margin replacement,
resizing, or private client protocol was added. This documents the remaining
CUPS PPD limitation rather than claiming full-width arbitrary Custom designs work.

[verified] The conventional queue also passes PNG/JPEG, registered 60/63, offline
queue/resume, chain collection, and existing Web-default preservation cases.
The final runtime build passes all 11 CTest suites in 120.50 s. Its isolated image
smoke passes four presets, synchronized defaults, explicit geometry, copies,
and malformed-input failure. Read-only focused review finds no actionable defect
in the fixes. The redundant CI target-build invocation was removed; runtime
building still requires the complete test stage before smoke and publication.
The test-only batch probe is retained as the reproducible protocol-gate fixture;
it is not a second production lifecycle or public protocol.

[verified] Current documentation records both passed physical sessions and keeps
historical checkpoints explicitly marked as history. Raw physical evidence,
operator observations, regressions, CUPS integration, full build, and smoke
results are retained under evidence/ with a SHA-256 manifest. No further labels
were printed, no commits or branches were pushed, and production was not changed.
Only specification sync, strict validation, and local archival remain.


## Local archival, 2026-10-13

[verified] The batch-printing, label-rendering, and printer-device specifications
were synced and compared against every delta requirement. Existing batch
purpose and scenario names were preserved. Strict validation passed all three
specifications and the active change before archival. All 42 tasks were checked
and verified through the CLI; archival completed locally with specs already
synced. Publishing and production deployment remain separate authorized actions.
