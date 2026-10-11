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

Both transfers succeeded. The baseline capture has 180 raster rows ending in
EJECT; the drain capture has the same row count ending in FF then a separate
EJECT. The raster payloads are byte-for-byte identical; the command differences
are advanced mode `08` versus `00`, and the drain's additional FF before EJECT.
Successful USB transfer did not establish successful printing. The proposed
immediate finalization sequence failed this physical comparison, so testing
stopped before the remaining matrix cases and before production driver changes.
The cause is not established; neither a timing delay nor a different terminal
sequence is proven by this result. Baseline cut-free status and measured length
are unconfirmed. Captures
and transfer logs are in `/tmp/ptouch-cut-probe-results-20261011` on `vault`
and `/tmp/ptouch-probe-check-20261011/hardware` locally.

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
