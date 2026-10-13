# Remaining physical acceptance

Status: **passed** on 2026-10-13 with 12 mm tape, all 21 complete labels and
negative/recovery trials. Operator confirms the layouts. The original service is
restored, with its image/configuration unchanged. The completed eight-label direct
protocol gate remains passed. This session checks the integrated candidate image
through ordinary IPP and verifies explicit recovery. It does not replace the
running service permanently.

## Bound and tape budget

Start with the operator-confirmed 12 mm cassette. If other widths are available, reuse
counted batches after a confirmed final cut for width/orientation checks; adapt
the imageable fixture to that cassette without adding labels. Send exactly
21 complete labels in the matrix
below, plus one deliberately incomplete raster upload and one blocked resubmission.
The latter two must issue no FF/EJECT; the incomplete upload can leave raster data
in the printer and is followed by explicit power-cycle recovery.

Most labels have 180 rows at 180 dpi, or 25.4 mm. The three-label raster document
uses 180/216/180 rows to expose individual lengths without a total-count attribute.
Printed content totals about **54 cm**. This excludes initial leaders and physical
feed allowances, which were not measured in the earlier gate; it is not a promise
of total consumed tape. No additional labels or repeats are part of this approval.
Stop and report any failed criterion before revising or repeating a stage.

## Matrix

| Stage | Submission | Labels | Required observation |
| --- | --- | ---: | --- |
| 1 | PNG, chain-only | 3 | First leader removed, labels separated, final end held |
| 2 | PNG, chain-only after an idle gap | 2 | Prior held label released, no discarded inter-batch leader, final end held |
| 3 | PNG, chain-and-final | 1 | Prior held end released, final label cut free |
| 4 | PNG, none | 2 | Initial leader removed, joined strip held |
| 5 | PNG, none | 2 | Prior strip released, second joined strip held, no discarded inter-batch leader |
| 6 | PNG, final-only | 1 | Prior strip released, final label cut free |
| 7 | One PNG with three copies, chain-and-final | 3 | Exactly three separated labels, one initial precut, final end cut |
| 8 | Three unknown-count raster labels, chain-and-final | 3 | Three complete separated labels with correct individual lengths/orientation markers, final end cut |
| 9 | One PNG with three copies, final-only | 3 | One joined three-label strip, cut only at batch end |
| 10 | Recovery label after the sequence below | 1 | Only the new marked label prints, with no buffered fragment or ghost replay |
| | **Complete labels** | **21** | |

Queue stages 2 and 3 while USB is disconnected, inspect their pending state,
then reconnect and inspect each batch before releasing the next. This reuses the
counted labels for physical offline recovery and healthy continuation. Confirm
cassette removal/reinsertion and readiness using status queries; mismatched-media
requests must fail without raster. Physical width certification is limited to
available cassettes; retain the existing distinction between five-width software
coverage and actual hardware evidence.

For uncertain-output recovery, after stage 9 send a single-page raster with a
valid header but an incomplete body. Confirm an aborted job with no print/feed
command. Submit the marked recovery label: it must be blocked without new output.
A clean status or USB reopen must not clear this condition. Power-cycle the printer,
then restart only the candidate server, and explicitly submit that recovery label
again for stage 10. Inspect failed job records before restart; process-local queue
persistence and automatic replay are not promised.

## Execution boundary

Before starting, record the current deployed image and confirm its queue is idle.
Temporarily yield USB to the isolated candidate, retaining the previous image and
configuration. Use one candidate process across healthy continued batches, with
no automatic reset or idle finalization. Capture job attributes, printer status,
and command data separately from the operator's physical observations. Restore
the original service only after the run is confirmed closed and recovery is safe.

Any changed label count, repeat, other tape width that needs extra printed labels,
or permanent deployment requires a separate instruction. Archive the OpenSpec
change only after the required physical evidence is recorded.

Evidence: `vault:/tmp/ptouch-integrated-acceptance.Nj73Wz/`, with a local
copy at `/tmp/ptouch-integrated-evidence.tar.gz`; detailed operator observations
and per-stage captures are recorded in the change's validation.md. Actual tape
consumption was not measured. No further physical labels are authorized.
