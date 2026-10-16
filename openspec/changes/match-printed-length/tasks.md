# Tasks

## 1. Physical geometry and usable media

- [ ] 1.1 Extend the existing pure geometry path with model-specific end margins and minimum length, keeping P710BT's 14-dot reservation per end and 24.5 mm minimum separate from unchanged PT-2730 geometry; verify dot/centimm conversion and minimum/maximum boundaries in the core geometry tests.
- [ ] 1.2 Use that geometry for media database/default/ready publication, supported top/bottom margins, and custom length validation; extend the existing PAPPL attribute bridge only where necessary, with adapter/CUPS assertions proving consistent P710BT attributes and unchanged PT-2730 attributes. Update README's media/short-length behavior and, if the bridge changes, `patches/README.md` with its bounded scope and removal criteria.

## 2. Rendering and transmitted print area

- [ ] 2.1 Extend the current PNG preflight to validate/compact blank full-page end margins after compositing, retaining original-canvas auto-length, orientation, DPI, and 1:1 positions; reject nonblank ends and below-minimum P710BT lengths before page setup. Extend `test_png_geometry` for explicit 36/50 mm lengths, omitted/source-only/margin-only length, all orientations, square DPI conversion, transparency, larger explicit margins, and minimum/maximum boundaries; confirm unchanged PT-2730 cases and document P710BT fit rejection in README.
- [ ] 2.2 Declare and transmit only the P710BT print-area rows while consuming every logical row and rejecting nonblank physical end rows; use the existing row index, pending-print lifecycle, and failure handling without new per-page state or buffers. Extend `test_driver`/raster integration checks for reconstructed physical pixels/lengths, equivalent 1/8-bit PWG and URF, mixed lengths, all four presets, cross-batch continuation, trailing-margin truncation, write/status failures, and byte-identical PT-2730 output. Update README's protocol explanation to distinguish logical and transmitted heights.
- [ ] 2.3 Extend the existing image smoke with fitting P710BT fixtures and reconstructed length/pixel assertions, preserving its PT-2730 expectations; verify both model selections against the built deployment image without USB and update affected fixture/documentation expectations rather than adding another harness.

## 3. Integrated software validation

- [ ] 3.1 Run `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure` with the pinned full dependencies, the adapter suite in the container, existing CUPS checks, and both model image smokes; retain passing output and the PT-2730 baseline/candidate byte comparison, with no physical behavior claimed from these checks.
- [ ] 3.2 Reconcile README and this change's validation record with actual software results, explicitly leaving physical validation to the user separately; verify there is no physical acceptance task, printer access, or service/configuration change in this change. Run the required cut pass and strict OpenSpec validation, then sync the delta specs and archive the software-verified change before remote delivery.
