# 8. Job-defined batches with explicit finishing and safe continuation

**Status:** Accepted; revised by simplify-print-server.

## Context

Queue contents and idle time do not describe whether a client wants a batch
finished. Earlier queue-drain finalization could miss jobs arriving during
printing. FF followed by an empty EJECT also failed on the PT-2730.

The former copy-based planner treated copies as labels, failed multi-label
raster with one copy, and imposed a 64-copy ceiling. PAPPL's legacy finishing
bitmask also loses registered 60/63 and the chain-only collection selection.
Sending ESC @ into a held run can wedge the printer, so preserving healthy
continuation remains necessary.

## Decision

One IPP job defines one batch. Expand PNG copies once; process actual raster
labels without requiring a declared total. Resolve original standard attributes
and the current synchronized printer default using one driver preset table:

| Preset | Between-label cuts | Final cut | Standard IPP selection |
|--------|--------------------|-----------|------------------------|
| Chain-only | Yes | No | finishings-col template ptouch-chain |
| Chain-and-final | Yes | Yes | 60; compatibility alias 11 |
| No label cuts | No | No | 3; collection no-value |
| Final-only | No | Yes | 63 |

The implementation-defined name ptouch-chain uses the standard finishing
collection; it adds no independent vendor attribute or private enum. It remains
per-job, because it cannot be represented losslessly as an enum printer default.
The initial canonical default is trim (11). Enum and collection updates to
representable defaults must stay synchronized; conflicting updates are rejected.

Precut prepares the first actual label of every nonempty batch, including none.
Later autocut depends only on between-label policy. The next batch releases the
held prior end without a discarded leader. Thus none/none creates separate
joined strips rather than one permanently joined multi-job strip.

Keep constant-size job state with two policy flags, actual-label progress,
one pending print command, and a failure latch. A next valid label releases the
prior complete label with FF and waits for receiving-ready. Successful batch end
releases the last label with FF for continuation, or late chain-off/EJECT when
finishing. Hold this decision until actual successful end-of-input; do not fully
feed and eject each label or buffer another raster image.

The queue owns closed/healthy-open/uncertain state and media confirmation.
Reset only a known closed run. Idle/job status queries confirm cassette and
errors without initialization or finalization. A canceled or rejected follower
that sent no page output preserves the previous healthy run. Ambiguous transfer,
decode, or readiness failures block further output until printer power-cycle
followed by server restart. Cleanup emits no pending feed, cut, or replay and
is idempotent.

Bounded pinned PAPPL corrections allow advertised enums/templates, original job
attributes, and synchronized defaults through the ordinary IPP boundary. The
CUPS/PAPPL raster correction distinguishes malformed termination from EOF.
Neither patch introduces a raster parser, spooler, or generic finishing engine.
See patches/README.md for causes, version/security policy, and removal criteria.

## Consequences

Default printing now cuts labels free; clients wanting continuation request it
explicitly. Cached CUPS defaults must be refreshed through standard queue
settings. Retire optional precut and old cutter environment settings. PNG copies
support 1–999 while streaming raster advertises one copy with actual multi-label
documents. Generic dialogs may not expose the chain-only template.

Run state is process-local, and unfinished-run restart is unsupported. Complete
a healthy run deliberately before restart; recover uncertain output explicitly
without promising queue persistence or automatic resubmission.

The eight-label direct-protocol PT-2730 gate on 24 mm tape passed all four modes,
mandatory precut, late finalization, and continuation without discarded leaders.
The separately authorized 21-label integrated acceptance on 12 mm tape also
passed PNG copies, unknown-count raster, idle/offline continuation, cassette
change, uncertain-output blocking, and explicit recovery. The operator confirmed
rectangles, different label lengths, orientation, and no ghost output. These
results certify the tested PT-2730 cases; 6/9/18 mm retain software-only coverage.
See tests/HARDWARE-ACCEPTANCE.md and the change's validation.md.
