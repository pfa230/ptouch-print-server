# Tasks

## 1. PAPPL job-history patch

- [ ] 1.1 In a PAPPL v1.4.11 checkout with the raster and finishing patches applied, make the `job_cb()` reason change (design D3) and the landing-page job table (design D2) in `pappl/printer-webif.c`. Export the diff as `patches/pappl-1.4.11-job-history.patch`. Verify that it applies with `patch --batch --forward --fuzz=0 -p1` after the other two and that PAPPL builds without new warnings.
- [ ] 1.2 Apply the patch after the existing two in `Dockerfile` and `Dockerfile.deploy`, and document it in `patches/README.md` with its removal criterion. Verify that both images build.
- [ ] 1.3 Extend `scripts/smoke-image.py`: after its existing input-failure job, fetch `/` once and assert that the job's row contains its failure message. Verify that both `--model PT-2730` and `--model PT-P710BT` smoke runs pass against a locally built deploy image.

## 2. Specific failure messages

- [ ] 2.1 In `src/driver.c`, set the print-output write-failure message and the distinct status-request message (design D4). Verify in `tests/test_driver.c`, for both models, by injecting a failure into a raster write and into the media-refresh status request. Assert each aborted job's message, and that the status-request case sent no page output.
- [ ] 2.2 Set the decode, oversize, and non-square messages in `src/filter_png.c` (design D4). Verify with a truncated-PNG case in `tests/test_png_geometry.c` that asserts "PNG decode failed".

## 3. Documentation and integration

- [ ] 3.1 In the README Deploy section, point the uncertain-output instructions to the web page's job list and failure reasons instead of `docker exec ... ptouch-app jobs`. Review the text against the `job-history` spec.
- [ ] 3.2 In the `Dockerfile` container, run `cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build --output-on-failure`, and both image smoke variants. Run `openspec validate show-job-failures --strict`. Report the results as core, adapter, and socket-IPP checks.
