# PT-2730 separate-finalization probe

This test-only executable compares the current terminal-label sequence with
continuation-mode printing followed immediately by a separate EJECT. It does
not change the app's callbacks or configuration. Capture generation works with
the pure C build; live transport requires the PAPPL/libusb container build.

The fixtures have edge bars and one, two, or three stripes. Their lengths at
180 dpi are 25.40, 30.48, and 35.56 mm (180, 216, and 252 raster rows). The core's
existing packing centers them within the selected tape's printable width.

## Capture and inspect without hardware

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
build/tests/ptouch-cut-probe --capture /tmp/drain.prn --sequence drain \
  --labels 3 --mode each --precut 1 --tape-mm 12
python3 tests/test_cut_probe.py --inspect /tmp/drain.prn
```

Omitting `--device` cannot contact the printer. The CTest capture test compares
both sequences for one/three labels and each/end with precut on/off. Its parser
consumes each raster payload before counting commands: bytes `0c` and `1a`
inside pixels are not counted as feeds or ejects.

## Exact sequences

Both variants open at most one device handle and send one ESC @ plus rasterstart.
They use `pt_plan_batch` and the same raster fixtures:

| Variant | Planner | Last page finish | Additional terminal command |
|---------|---------|------------------|-----------------------------|
| baseline | `chain_out=false` | chain-off `08`, EJECT | none |
| drain | `chain_out=true` | chain-on `00`, FF | immediate EJECT |

There is no sleep, extra raster, reset, or handle close between the final FF and
the separate EJECT. Each command/row is flushed; live mode checks the difference
in PAPPL's transferred-byte metric before proceeding. A failed transfer stops
the probe without automatically replaying content. The capture records attempted
commands; only exit status zero establishes that every transfer was reported
successful. Neither that status nor a socket capture proves physical printing.

For `each` with precut off, the baseline's final label has autocut off (`00`)
while the drain version has autocut on (`40`) and PRINTINFO. The final label's
physical output, including the absence of a new leader cut, must be compared.
The known `end` plus precut-on defect is retained; its bytes are tested, but this
probe does not claim to fix it.

## Verified software results (2026-10-11)

- The pinned `Dockerfile.deploy` build stage compiled the unchanged app and
  the probe with PAPPL 1.4.11. All five core CTest tests passed.
- The host capture test passed all four tests, covering 16 baseline/drain
  variants, raster payload parsing, truncation, and invalid arguments.
- With the PAPPL-linked probe and a local TCP sink, each three-label `each`,
  precut-on variant used one connection. The received stream exactly matched
  its capture: 12,369 bytes for baseline, 12,370 for drain. Every checked
  transfer succeeded. Drain ended with FF followed by a separate EJECT.
- Strict OpenSpec validation passed. The physical gate has not passed.
- After approval, the probe was staged at `/tmp/ptouch-cut-probe-20261011`
  on `vault`; its help command ran successfully in the deployed runtime image
  with no USB access. The hardware preflight found no Brother (`04f9`) USB
  device in `vault`'s sysfs, and the server logged repeated device-open failures.
  No physical comparison could run; the hardware gate remains open.

## Failed hardware gate (2026-10-11)

The user confirmed 24 mm tape and readiness to inspect the PT-2730 output.
The printer subsequently enumerated on `vault` as USB `04f9:2041`; all existing
jobs were completed before testing. Each probe ran with the normal server
stopped, and the server was restored to running afterward.

| Sequence | Mode | Precut | Labels | Checked USB bytes | Physical observation |
|----------|------|--------|--------|-------------------|----------------------|
| baseline | each | 1 | 1 | 3,443 | User reported one leader and one printed rectangle. |
| drain | each | 1 | 1 | 3,444 | User reported one leader only, with no printed rectangle. |
| tail-eject | each | 1 | 1 | 3,443 | User corrected the initial report: one leader and two rectangles, with the second rectangle uncut. |
| baseline | each | 1 | 3 | 3,630 before failure | Transfer failed during the second page; the paired tail-eject run did not start. |
| tail-chain-off | each | 1 | 1 | 3,447 | User reported one leader, one rectangle cut free, and nothing more. |
| tail-chain-off | each | 1 | 3 | 3,630 before failure | Transfer failed during the second page at the same byte count as baseline; physical output pending. |

Both transfers succeeded. The baseline capture has 180 raster rows ending in
EJECT; the drain capture has the same row count ending in FF then a separate
EJECT. The raster payloads are byte-for-byte identical; the command differences
are advanced mode `08` versus `00`, and the drain's additional FF before EJECT.
Successful USB transfer did not establish successful printing. The proposed
immediate finalization sequence failed this physical comparison, so testing
stopped before the remaining matrix cases and before production driver changes.
The original separate-finalization mechanism failed; subsequent diagnostic
evidence is recorded below. Baseline cut-free status and measured length
are unconfirmed. Captures
and transfer logs are in `/tmp/ptouch-cut-probe-results-20261011` on `vault`
and `/tmp/ptouch-probe-check-20261011/hardware` locally.

## Focused follow-up probes

`--sequence tail-eject` replaces the last continuation FF with EJECT, without
adding a second print command. It still leaves the last rectangle uncut according
to the corrected physical observation. `--sequence tail-chain-off` preserves
the baseline's autocut settings, starts the last page with advanced mode `00`,
then sends advanced mode `08` immediately after its raster and before its only
EJECT. This isolates whether the final chain setting can be selected after the
raster; its single-label default case physically cut the rectangle free, but
its three-label case failed transferring page two. Neither variant adds raster rows or
a sleep. The six capture tests verify the diagnostic byte changes and preserved
fixture pixels and autocut settings.

`--status-only` queries the PT-2730 through the existing device scheme, without
initialization, raster, feeding, or cutting, and reports its raw status.

`--wait-ready 1` adds a diagnostic USB receive step after each page terminator,
including the final one. It drains old statuses before initialization and logs
unsolicited 32-byte packets, stopping on a reported error or missing completion.
It sends no status requests while printing and changes no captured print bytes.
The receive loop is bounded to 70 reads with the existing 300 ms read timeout.
Only receiving-ready (type 06, phase 00, zero phase number) ends the wait.
An initial variant accepted type 01 (print-completed) too early: page one
produced printing then completed-with-printing-phase, and page two consumed
the preceding page's ready packet. That run transferred 7,751 bytes before
failing during page three. The corrected receiving-ready predicate awaits
physical verification. The production design now uses explicit job finishings
instead of the superseded queue-drain finalization mechanism.
This tests whether the repeatable second-page transfer failure follows sending
new commands while the printer is still busy; that cause is not yet proven.
The seven capture tests and five core CTest tests pass with this probe addition.

Brother's [raster reference for newer models](https://download.brother.com/welcome/docp100407/cv_ptp900_eng_raster_102.pdf)
describes FF and `1A` as page-ending print commands and specifies that advanced
mode bit 3 controls terminal feeding/cutting. The PT-2730-compatible
[ptouch-rs implementation](https://github.com/vowstar/ptouch-rs/blob/main/crates/ptouch-core/src/session.rs)
uses actual blank raster when implementing a standalone feed/cut. These are
evidence for the diagnostic changes, not proof of PT-2730 behavior. A failed
physical gate blocks production changes while investigation continues.

## Hardware gate

Use only the PT-2730, with the declared tape actually loaded. Run the baseline
and drain variants individually for this matrix, checking each output before
moving on:

| Mode | Precut | Label counts |
|------|--------|--------------|
| each | 1 | 1, 3 |
| each | 0 | 1, 3 |
| end | 0 | 1, 3 |

Build the probe with PAPPL 1.4.11 and libusb. The deployed runtime contains no
compiler or headers, so build in the `Dockerfile.deploy` build stage and then
build its `ptouch-cut-probe` target. Run the resulting binary in a compatible
runtime with USB access.

When hardware testing is authorized, release the USB interface from the normal
server before running the probe, then restore that service afterward. Use pfa's
SSH connection to `vault`; do not replace the deployed app with the probe.
Example for one case, with the compatible binary already available:

```sh
ptouch-cut-probe --capture /tmp/baseline-each-1-1.prn --sequence baseline \
  --labels 1 --mode each --precut 1 --tape-mm 12 \
  --device ptouch://Brother/PT-2730
ptouch-cut-probe --capture /tmp/drain-each-1-1.prn --sequence drain \
  --labels 1 --mode each --precut 1 --tape-mm 12 \
  --device ptouch://Brother/PT-2730
```

For each case record: loaded tape, variant, mode, precut, label count, physical
leader count, cuts between labels, measured lengths, whether the final label
is cut free, any extra blank label/tape, and transfer result. Stop on failure
and report the observation. Do not mark the hardware gate complete from stream
counts, successful writes, or assumed physical results. Production driver changes
depend on this gate passing.

## Explicit-finishing candidate, three-copy USB test (2026-10-11)

Candidate commit `b5fa376` ran in a temporary network-disabled container on
`vault`, using only the PT-2730's USB node. The normal server had no jobs and was
stopped for the test, then restored to running; the deployed binary/image was
not replaced.

A real IPP Print-Job requested `finishings=11` (trim) and three copies of a
170x180-pixel generated PNG showing a framed X on user-confirmed 24 mm tape.
The PNG filter resolved it to 180 rows per copy (25.40 mm), with the existing
24 mm margins and printable window. PAPPL logged `finishings=0x4`, `copies=3`,
and Completed with three impressions.

Every page produced the same unsolicited status sequence: type 06/phase 01
(printing), type 01/phase 01 (completed while still in printing phase), then
type 06/phase 00 (receiving-ready). All reported error bits were zero. The driver
consumed receiving-ready before sending the next copy. The first two ready
notifications followed their page submissions by about 4.9 seconds each; the
terminal copy took about 8.7 seconds. Earlier probes sent the next page
immediately and their two-second USB writes failed during that page.

This is a successful real-USB transfer/status result with a changed readiness
boundary. Physical leader count, actual copy count/cuts, and measured lengths
await the user's observation and cannot be inferred from Completed alone. The
log is `/tmp/ptouch-finishing-hardware-20261011/trim-three-copies.log` on vault
and in the same directory locally. Explicit none/trim runs, idle gaps, precut
off, and hardware offline recovery remain open gates.
