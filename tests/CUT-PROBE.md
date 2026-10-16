# Cutter protocol and PT-2730 validation

The optional `ptouch-cut-probe` target exercises cutter command sequences without
changing the application driver. Socket captures verify bytes; real USB tests
and physical observations establish whether labels print and cut correctly.
Apart from the [PT-P710BT held-label release](#pt-p710bt-held-label-release)
below, only the Brother PT-2730 has physical evidence. The four-mode `batch` gate
below passed its bounded eight-label check on 24 mm tape; the separate 21-label integrated application
and recovery acceptance also passed on 12 mm tape.

## Build and run the probe

Build with the pinned PAPPL/libusb environment provided by `Dockerfile.deploy`:

```sh
cmake -S . -B build
cmake --build build --target ptouch-cut-probe
```

Use a compatible runtime with USB access. Release the printer interface from
other software before testing and restore normal access afterwards. For example:

```sh
ptouch-cut-probe --capture /tmp/baseline.prn --sequence baseline \
  --labels 1 --mode each --precut 1 --tape-mm 24 \
  --device ptouch://Brother/PT-2730
```

Record the loaded tape, sequence, mode, precut, label count, leader count,
separation between labels, measured frame length, final cut, extra blank tape,
and transfer result. A successful transfer alone does not establish physical
printing or cutting. Stop a failed test without automatically replaying it.

`--status-only` sends a status request without initialization, raster, feeding,
or cutting. `--wait-ready 1` consumes unsolicited status after each page and
waits for receiving-ready: type 06, phase 00, zero phase number. The loop is
bounded to approximately twenty seconds with a 300 ms read timeout and sends no requests while
printing.

## Four-mode candidate protocol gate

`--sequence batch` exercises the proposed deferred-command lifecycle without
changing the production driver or using its copy-based planner. It prepares each
label with print information, autocut flags, and chain-on; supplies all raster;
and leaves that label's print command pending until the next label starts or the
batch succeeds. Intermediate labels receive FF. A continuing batch ends with FF;
a finishing batch changes chain-off after the last raster and sends one EJECT.
There is no empty-page FF/EJECT pair. Mandatory precut enables autocut only on the
first label, unless the mode also requests cuts between later labels.

| Probe mode | Cut between labels | Cut after batch | First label precut |
| --- | --- | --- | --- |
| `chain` | Yes | No | Yes |
| `each` | Yes | Yes | Yes |
| `none` | No | No | Yes |
| `end` | No | Yes | Yes |

Each batch is bounded to one, two, or three labels. `--batches` accepts at most
three batches with at most six labels total, expanding each batch separately
without resetting between healthy unfinished batches. `--continue 1` omits the
first initialization only for a known healthy open run from a successful earlier
probe. These are test-only controls, not application configuration or IPP options.
The candidate rejects precut off and requires readiness waiting for USB output.

Generate fixtures safely without a device argument:

```sh
ptouch-cut-probe --capture /tmp/batch-end-3.prn --sequence batch \
  --labels 3 --mode end --precut 1 --tape-mm 24
ptouch-cut-probe --capture /tmp/batch-chain-matrix.prn --sequence batch \
  --batches chain:3,chain:2,each:1 --precut 1 --tape-mm 24
ptouch-cut-probe --capture /tmp/batch-none-matrix.prn --sequence batch \
  --batches none:2,none:2,end:1 --precut 1 --tape-mm 24
python3 tests/test_cut_probe.py --inspect /tmp/batch-end-3.prn
```

The Python capture suite verifies four modes at all three bounded label counts,
one print command per rendered label, late finalization ordering, mandatory
first-label-only precut in final-only/no-cut modes, resets, and both continuation
matrices. Raster payload is decoded by length, so command-like pixel values do
not create false cuts. Unique stripe counts distinguish labels; edge bars mark
the full intended length. Captures establish the transferred command policy,
not whether this model interprets the final command as intended.

### Approved eight-label initial gate

The user approved the reduced initial gate: four two-label batches on 24 mm
tape. The earlier 27-label proposal is superseded. This gate checks all four
modes and continuation; the separate integrated acceptance matrix subsequently
received its own authorization and passed on 12 mm tape.

Stop the idle printer server to release the USB interface. Confirm a clean live
status and 24 mm cassette, and power-cycle before the first fresh run. A later
fresh run can initialize after the preceding final cut is physically confirmed
and receiving-ready was acknowledged; power-cycle after any uncertain output.
Use the existing compatible runtime for this test-only probe; do not replace
the deployed server image. Retain captures and stdout/stderr for every command.

First run two chain-only labels:

```sh
ptouch-cut-probe --capture /tmp/gate-chain-2.prn --sequence batch \
  --labels 2 --mode chain --precut 1 --tape-mm 24 --wait-ready 1 \
  --device ptouch://Brother/PT-2730
```

Check one initial leader, label 1 separated, and label 2 held. After a short idle
gap, continue without resetting:

```sh
ptouch-cut-probe --capture /tmp/gate-each-2.prn --sequence batch \
  --labels 2 --mode each --precut 1 --tape-mm 24 --wait-ready 1 --continue 1 \
  --device ptouch://Brother/PT-2730
```

Both new labels and the previous held end should be released, with no discarded
leader between batches. This checks late chain-off after a multi-label batch.

After confirming the preceding final cut, start a fresh run with two no-cut labels:

```sh
ptouch-cut-probe --capture /tmp/gate-none-2.prn --sequence batch \
  --labels 2 --mode none --precut 1 --tape-mm 24 --wait-ready 1 \
  --device ptouch://Brother/PT-2730
```

Check one initial leader and one joined, held two-label strip. Continue after a
short idle gap without resetting:

```sh
ptouch-cut-probe --capture /tmp/gate-end-2.prn --sequence batch \
  --labels 2 --mode end --precut 1 --tape-mm 24 --wait-ready 1 --continue 1 \
  --device ptouch://Brother/PT-2730
```

The new batch's precut should release the prior strip. The new two labels should
remain joined and be cut free only at batch end, with no discarded inter-batch
leader. Restore the original server after the probes complete safely.

Each batch uses 180/216 rows at 180 dpi, framing 25.4/30.48 mm. Eight labels total
223.52 mm of framed raster, plus two fresh-run leaders and physical feed margins.
This is approximately 22 cm of label content; record actual tape use. There is
no image decoder or production leading-row correction in this direct probe.

The same eight-label command policy can be checked without USB access using
`--batches chain:2,each:2` and `--batches none:2,end:2`, omitting `--device` and
`--wait-ready`. Captures must show one initialization and one EJECT per pair,
with first-label precut and the expected later-label autocut flags. Single-label,
three-label, and other transition cases remain covered in the capture suite;
they do not consume tape in this initial gate.

Record loaded tape, mode order, stripe/edge visibility, leader count, each
separation, held final ends, measured lengths, readiness/error packets, and
any discarded tape. Pause for physical observations after each batch. Any
transfer failure, printer error, readiness timeout, or incorrect physical output
stops the gate immediately; do not automatically replay, feed, cut, or reset.
Power-cycle before further work after uncertain output. Revise an unsuccessful
command policy before enabling it in production, retaining all required modes
and unknown-count support.

## Diagnostic findings

### Eight-label gate result

The user confirmed these physical results for the approved initial gate:

| Batch | Observation |
|-------|-------------|
| Two chain-only labels | Initial leader cut off; first rectangle separated; final rectangle held |
| Two chain-and-final labels | Prior held rectangle released, both new labels separated, final cut; no discarded leader |
| Two no-cut labels | Initial leader cut off; rectangles remained joined and the end held |
| Two final-only labels | Prior double-label strip released, new double-label strip cut free; no internal cut or discarded leader |

All eight labels received clean completion and receiving-ready notifications.
Captures contain the expected FF/EJECT commands and no initialization between
healthy continued batches. The deployed server image was not replaced, and the
original server was restored afterward. The acceptance summary is in the
OpenSpec change's validation.md.
The separately authorized 21-label integrated acceptance subsequently passed
on 12 mm tape, including PNG/raster batches and recovery. See
HARDWARE-ACCEPTANCE.md and the change's validation.md. Software-only geometry
coverage for 6/9/18 mm does not claim physical certification.

### Earlier probes

The original queue-drain finalization proposal was superseded by explicit job
finishings. Its probes remain useful evidence:

| Sequence | Observed result |
| --- | --- |
| baseline, one label | One leader and one printed rectangle |
| drain: FF followed by a separate EJECT | One leader only; no printed rectangle |
| tail-eject | The final rectangle remained uncut |
| late chain-off after raster, one label | One printed rectangle cut free |
| multiple labels without correct readiness handling | Transfer failed during a later page |

The baseline and drain captures contained identical raster pixels, so successful
byte transfer did not prove the separate-finalization proposal worked.

Print-completed status (type 01) can precede receiving-ready. Accepting it too
early left a ready packet from the preceding page to satisfy the next wait;
waiting for the actual receiving-ready notification resolved the multi-page
transfer failure. The driver uses the normal chain-off/EJECT sequence, rather
than FF followed by a standalone EJECT or the late-chain-off diagnostic variant.

Brother's [raster reference](https://download.brother.com/welcome/docp100407/cv_ptp900_eng_raster_102.pdf)
and the [PT-2730-compatible ptouch-rs implementation](https://github.com/vowstar/ptouch-rs/blob/main/crates/ptouch-core/src/session.rs)
help interpret these commands. Physical PT-2730 observations remain the evidence
for this printer's behavior.

## Historical verified finishing matrix

Tests used 24 mm tape and a framed X fixture with 180 rows at 180 dpi. Software
checks cover command captures, resolved IPP options, checked transfers, and status
handling; the following results were observed on the physical printer:

| Test | Physical observation |
| --- | --- |
| Three trim copies, precut enabled | One leader, then three X labels, all cut apart including the last |
| Separate none, none, trim jobs with seven-second idle gaps | Leader attached to two joined X labels, cut, then one X label, cut |
| Trim copies, precut disabled, original flags 40/40/00 | The final two copies remained joined |
| Trim copies, precut disabled, corrected flags 00/40/40 | Leader attached to the first X label; all copies cut apart, including the last |
| Queued trim job while printer powered off, then power-on | Leader, cut, X label, cut |
| None, none, trim with intervening status queries | Joined strip preserved; same cuts as the idle-gap test |
| Single trim label | Leader, cut, X label, cut |

These observations concern the earlier two-mode production behavior, including
optional precut. They do not establish the revised four-mode contract, mandatory
precut, or separation of consecutive none batches without wasted leaders.

The printed frame's long edge measured approximately one inch, consistent with
25.4 mm for 180 rows at 180 dpi. No extra blank label was observed. A precut-off
single-copy trim job kept its leader attached and cut its final label free.

The copy-boundary correction leaves autocut disabled for the first copy and
enabled before subsequent copies, including the last. The final EJECT separates
the last copy. Non-resetting status queries also preserve continuation while
allowing cassette-width changes and missing tape to be detected before creating
subsequent job geometry.

The corrected runs reported zero printer error bits and consumed receiving-ready
after every page. The physical results, together with the software checks in
[FINISHINGS.md](FINISHINGS.md), establish the finishing behavior.

## PT-P710BT held-label release

This direct hardware test on 2026-10-16 selected the mechanism behind Ptouch-Cut. It was recorded in diagnostic commit `9e239ae` (`tests/CUT-ONLY-PREP.md` on the local `test/cut-only-probe` branch). It used PT-P710BT over Bluetooth (RFCOMM channel 1) on the Pi at `ptouch-bt.iot.pfa.name`, with 12 mm TZe tape. The production service was stopped and test-only fixtures were sent outside the IPP server. Before each candidate, a marked label printed in chain mode reached receiving-ready, and the operator confirmed that its end was held. Each candidate was sent once, without retries.

| Candidate | Bytes | Status | Physical result |
| --- | --- | --- | --- |
| Chain off, then EJECT | `1b 69 4b 08 1a`: 5 bytes, no initialization, page setup or raster | No response; the 20-second completion deadline expired | Not released |
| Zero-raster page from [`nbuchwitz/ptouch` af16cac](https://github.com/nbuchwitz/ptouch/commit/af16cac251baa845d77fd2ccde6bdac6ed926bf3) | 239 bytes: initialization, page settings declaring zero rows, auto-cut on, chain off, margin, compression, EJECT | Clean | Not released |
| One white row as a continuation page | The 57 bytes below | Printing (type 6, phase 1), print-completed (type 1), receiving-ready (type 6, phase 0), zero error bits | Operator confirmed the preceding label cut free |

Between the two failed candidates, a normal marked label with chain off and EJECT printed and cut free, confirming the connection and cutter. The selected sequence:

```text
1b696101 1b692100 1b697a84000c00010000000100
1b694d40 1b694b00 1b69640e00 4d02
4711000f 00000000000000000000000000000000 0c
```

It selects raster mode and automatic status, declares one continued row (page flag 1) on 12 mm tape, enables auto-cut and chain mode, keeps the 14-dot margin and PackBits compression, sends sixteen white bytes as one literal row, and ends with FF. It sends no invalidate, initialize, or EJECT. `pt_cmd_blank_continuation` builds these bytes, varying only the tape width, and Ptouch-Cut sends them.

The row is 0.141 mm of raster at 180 dpi; the total tape feed and the remaining blank tail were not measured. The blank page remains held until the next job's precut. Both failed candidates transferred successfully, so a successful write alone is not evidence of a cut. This test does not establish a raster-free cutter command, PT-2730 behavior, other tape widths, or USB. Those, and the integrated IPP operation, have software captures only (`test_protocol`, `test_driver`). The raw JSONL records were staged temporarily; the bytes and observations above are the retained record. The original service was restored afterwards.
