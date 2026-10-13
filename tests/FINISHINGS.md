# Finishing verification

The contract is one IPP job per batch, four policies, mandatory first-label
precut, and no discarded leaders during healthy continuation. Registered enum
choices are none (3), trim (11), trim-after-pages (60), and trim-after-job (63).
Chain-only uses the implementation-defined ptouch-chain template through
standard finishings-col. Collection no-value selects none.

## Software checks

Run the full build in the pinned container environment:

```sh
cmake -S . -B build -DPTOUCH_CORE_ONLY=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

The hardware-free cutter and capture suites check the two policy flags and
observable command order, including one/three labels and the eight-label gate.
They use no copy-sized operation array or planner API. Historical diagnostic
probe sequences remain test-local.

Real IPP driver tests exercise actual PNG copies and unknown-count raster labels,
all four presets, aliases, defaults, explicit collections, supported attributes,
and rejected/conflicting requests. A counting sink verifies 65 and 999 PNG copies;
streaming formats advertise one copy. Failure fixtures cover raster, FF, EJECT,
decode, cancellation, and fragmented receiving-ready. Repeated cleanup must
emit nothing; clean status/reopen must not clear uncertain output.

The minimal PAPPL finishing fixture isolates advertised-attribute acceptance,
legacy-driver compatibility, and atomic enum/collection default updates. The
raster fixture covers malformed first/next headers, incomplete rows/pages, and
negative input callbacks before successful end-job cleanup.

`python3 scripts/smoke-image.py IMAGE --model PT-2730` and the same command with
`--model PT-P710BT` run the built deploy image, submit real
IPP PNG jobs for all four policies and the omitted default, and capture actual
socket output. It checks explicit 50 mm pages, copies, public capabilities, and
malformed-image failure without USB access. This requires Docker and ipptool.

Production-driver tests execute the same finishing and failure fixtures for both
models. Length-aware P710BT decoding checks TIFF PackBits rows, actual page
heights, notification, feed margin, and first/subsequent physical-run page flags.
PT-2730 continues to use legacy setup and raw rows, with no P710BT-only commands.
The common deferred-print and closed/open/uncertain lifecycle remains unchanged.

`scripts/check-cups.py /path/to/ptouch-app` runs inside an isolated container
with CUPS, cups-filters, Ghostscript, ipptool, and Pillow. It creates an ordinary
driverless queue, submits PNG/JPEG, two-label PDFs, and two copies of a PDF with registered finishing
choices, queues two jobs while the socket printer is offline, then captures their
resumption. It also checks standard chain collection submission and that saving
unrelated fields in the existing Web UI preserves trim/final-only defaults.
Custom-size cases cover CUPS width rounding with fitting explicit-margin content,
oversized rejection without output, and exact standard media-col dimensions.

The change's validation.md records execution results separately from physical
printer observations.

## Physical evidence

P710BT integrated acceptance passed on 2026-10-13 under
[P710BT-ACCEPTANCE.md](P710BT-ACCEPTANCE.md): 15 complete labels including four
operator-requested repeats, all four policies, healthy continuation, unknown-count
mixed lengths, strict wrong-media rejection, USB-only offline resumption, and
button-power-cycle/server-restart recovery without replay. Physical geometry
was checked on 9/12/18 mm; 6/24 mm remain software-only. Every command capture
matches its socket reference after removing status requests. The candidate was
removed; the original service's image/configuration remains unchanged.

The remaining evidence below concerns PT-2730.

The approved eight-label direct-protocol gate passed on the PT-2730 with 24 mm
tape: chain:2 then each:2, and none:2 then end:2, with an observation/idle gap at
each batch boundary. The operator confirmed initial leaders cut off, expected
internal/final cuts, held ends released by the following batch, and no discarded
inter-batch leaders. See CUT-PROBE.md for exact commands and the change's
validation.md for the acceptance summary.

The separately authorized integrated acceptance also passed all 21 labels on
12 mm tape, including PNG/raster batches, cassette change, offline continuation,
uncertain-output blocking, and power-cycle/server-restart recovery without ghost
output. The operator confirmed length/orientation markers and all four cut
policies. Other tape widths have software coverage only, except the 24 mm direct
protocol gate. See HARDWARE-ACCEPTANCE.md and the change's validation.md. The
deployed service image has not been replaced.
