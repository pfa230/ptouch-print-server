# Tasks

## 1. Queue cuts with printer work

- [x] 1.1 Extend the existing PAPPL printer-operations patch with queued acquisition and integrate it into the cut handler, including job-start ordering and request-record cleanup. Replace the old reentrant busy checks in `tests/test_driver.c` with coordinated concurrent requests; verify older prints, interleaved cuts, later PNG/raster jobs, and status polling obey the delta's ordering and that responses follow execution. Run these adapter tests against patched PAPPL in the existing container build; existing cut bytes, authorization, no-op, and failure cases must still pass.
- [x] 1.2 Update the README's cut instructions and `patches/README.md` for queued ordering, synchronous completion, and client timeout implications. Verify the documented result and unavailable-printer behavior agree with the delta and adapter tests, and document the patch's scope and removal condition.


- [x] 1.3 Resolve review 3's two scheduler-progress findings: skip streaming work behind a cut boundary before allowing its request-owned status to stop the active-job scan, and resume eligible scheduling when an unowned canceled stream finishes. Extend the existing driver harness with (a) an earlier held PNG released while a later streaming job waits behind its cut and (b) a pending PNG behind a cut whose closure encounters a canceled streaming request before that request finishes. Verify both cases reach terminal states without another client action, preserve exclusive ownership, and run the adapter tests with the existing ordinary-cut cases.

## 2. Integration checks

- [ ] 2.1 Run `cmake -S . -B build -DPTOUCH_CORE_ONLY=ON`, `cmake --build build`, and `ctest --test-dir build --output-on-failure`; build `Dockerfile.deploy` so its full adapter CTest suite runs, run `scripts/smoke-image.py` against that image for both PT-2730 and PT-P710BT, and run `openspec validate --all --strict --no-interactive`. Report the actual results as software validation; do not claim fresh physical cut validation.
