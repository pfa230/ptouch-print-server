# Cutter protocol and PT-2730 validation

The optional `ptouch-cut-probe` target exercises cutter command sequences without
changing the application driver. Socket captures verify bytes; real USB tests
and physical observations establish whether labels print and cut correctly.
Only the Brother PT-2730 is hardware-certified.

## Build and run the probe

Build with the pinned PAPPL/libusb environment provided by `Dockerfile.deploy`:

```sh
cmake -S . -B build -DPT_PROBE_TRANSPORT=ON
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
bounded to 70 reads with a 300 ms read timeout and sends no requests while
printing.

## Diagnostic findings

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

## Verified finishing matrix

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
