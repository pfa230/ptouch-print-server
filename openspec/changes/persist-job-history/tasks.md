# Tasks

## 1. Preserve diagnostic metadata safely

- [ ] 1.1 Extend the existing pinned PAPPL job-history patch to save/load optional message text, correct the pre-existing loader rejection of Job records without `format`, and restore all unfinished jobs as aborted completed history, removing their input files before scheduling; verify message roundtrips (including quotes and percent signs), saved processing/stopped jobs with surviving inputs, and an unfinished job awaiting data saved without `format` in focused state fixtures based on `tests/test_pappl_finishings.c`, with the interruption/resubmission message for unfinished jobs and no active jobs or device output.
- [x] 1.2 Update `patches/README.md` for the correction's scope and removal criteria; verify strict patch application in both existing Docker build paths.

## 2. Load before creating the queue

- [ ] 2.1 Honor the existing spool-directory option in `src/main.c`, register the persistent save callback and load once before creating a missing configured queue; verify the actual app with a selected directory starts with exactly one matching queue and retains saved history.
- [ ] 2.2 Extend `scripts/smoke-image.py` to replace a container against the same mounted directory after its successful, malformed-PNG, and canceled-before-data jobs; explicitly verify the canceled job saved without `format` survives as canceled history, then verify IDs, saved fields, exact failure explanations through IPP and web history, advancing new-job IDs, zero replay, and existing disabled completed-document retention. Recreate the container with a conflicting configured printer identity against the same directory; verify startup fails without changing the saved state file or its job history (interrupted payloads are cleaned during load), and starts with the replacement configuration after selecting fresh storage. Update README history/recovery text to match these outcomes and the explicit directory option.

## 3. Retain state outside the container

- [ ] 3.1 Set the image entrypoint's existing spool-directory option to `/var/lib/ptouch` and bind `/data/ptouch/state` there in `deploy/pi/user-data`; add matching persistent mounts to the USB Compose and manual Bluetooth examples. Verify the rendered service creates/uses the directory on `/data`, matches the image path, and retains existing host networking, journald and USB settings; the image smoke must exercise this entrypoint.
- [x] 3.2 Document the installed-Pi service migration, intentional printer-change procedure (stop, move old state aside, create fresh storage, reconfigure/pair, start), last-successful-save limitation, lack of backfill, and rollback in `docs/PI-DEPLOYMENT.md`; update `ptouch-pair` output to report pairing/configuration success accurately and point to the recovery for changed printers. Review commands against the read-only-root maintenance procedure and distinguish application history from journal logs and printer run state.

## 4. Integration verification

- [ ] 4.1 Run the full `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure` with pinned patched dependencies, build `Dockerfile.deploy`, run its image smoke for PT-2730 and PT-P710BT, and run `openspec validate --all --strict --no-interactive`; record results as software/container evidence, with real Pi reboot persistence requiring separately authorized deployment validation.
