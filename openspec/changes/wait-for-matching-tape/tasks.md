# Tasks

## 1. Wait for matching tape in the driver

- [ ] 1.1 In `tests/test_driver.c`, add failing cases that call `r_startjob` and `r_startpage` directly on the USB fixture with a 12 mm page: (a) status snapshots report 18 mm, then 12 mm; assert the page starts on 12 mm, `media-ready` becomes 12 mm, and no bytes other than status requests (`ESC i S`) were written before the 12 mm snapshot; (b) the job is canceled while 18 mm is loaded; assert `r_startpage` returns false with no output beyond status requests; (c) the status read fails during the wait; assert the abort message names the unreadable tape and nothing else was written; (d) a later label with a different width fails without waiting. Run each case for `PT-2730` and `PT-P710BT`, and verify that (a) through (c) fail against the current driver.
- [ ] 1.2 In `src/driver.c`, split the `r_startpage` guard into its tape-independent and width parts, find the supported tape that matches the page width, and add the wait loop from design D1 and D2 before the run-state change. Verify that the 1.1 cases pass, along with the existing `test_driver`, `test_png_geometry`, and `test_pappl_raster` suites, by running `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure` in the `Dockerfile` container.
- [ ] 1.3 Confirm that the socket-transport wrong-tape checks still abort without output: `tests/test_png_geometry.c` media cases 6 and 9, and the `om_18x50mm` submission in `scripts/smoke-image.py`. Verify by running both image smoke variants (`--model PT-2730`, `--model PT-P710BT`) against a locally built deploy image.
- [ ] 1.4 Update the README Capabilities bullet that says a wrong-width job is rejected: a job for another supported width now waits, reports `media-needed`, and prints after the swap; cancel it to give up. Review the text against the label-rendering delta.

## 2. Hardware acceptance

- [ ] 2.1 On PT-2730 with 18 mm tape loaded, submit one 12 mm PNG through the deployed queue and check that the job shows the waiting message and the printer reports `media-needed`. Open the cover, swap to 12 mm tape, and close it; confirm that exactly one label prints at the right size with no extra leader, and that a second queued 18 mm-width job then waits in turn. Cancel that one and confirm that nothing prints. Record the result in `tests/HARDWARE-ACCEPTANCE.md`.
- [ ] 2.2 Repeat 2.1 on PT-P710BT with 9, 12, and 18 mm tape, and record the result in `tests/P710BT-ACCEPTANCE.md`.

## 3. Integration

- [ ] 3.1 Run `openspec validate wait-for-matching-tape --strict` and confirm it passes. Report the results separately as core tests, adapter tests, socket-IPP smoke, and hardware acceptance.
