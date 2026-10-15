# Tasks

## 1. Configuration and build dependency

- [x] 1.1 Add `libbluetooth-dev` to the build environments in `Dockerfile` and the build stage of `Dockerfile.deploy`; verify both images build and that `ldd ptouch-app` lists no new library.
- [x] 1.2 Parse `PTOUCH_BT_ADDRESS` and `PTOUCH_IDLE_POLL_SECONDS` once at startup in `src/main.c`, failing on a malformed address, an address with any model other than PT-P710BT, or a polling value that is not a non-negative integer; verify each accepted and rejected case in a startup test, and that both unset leaves the USB queue URI and identity unchanged.
- [x] 1.3 Document both variables and their defaults in README; check the text against the 1.2 test cases.

## 2. Idle polling control

- [x] 2.1 Rate-limit `status_cb` by the parsed polling value, timing from the start of each idle probe attempt (absent: unchanged; `0`: no device access; `N`: at most one attempt per `N` seconds, keeping the last published status); verify in `test_driver` with a fake clock that unset matches current behavior, `0` opens no device across repeated calls, `N` attempts once per window both when reads succeed and when the open fails, and job-owned media refresh still runs with polling `0`.

## 3. Bluetooth transport

- [x] 3.1 Add `src/device_bt.*` registering the `ptouch-bt://Brother/PT-P710BT/<address>` scheme on RFCOMM channel 1, with a 10-second non-blocking connect, full write, 300 ms assembling status read (requested bytes, partial count at timeout, 0 if nothing arrived, -1 on EOF or error) and close; verify in a new adapter test using a socketpair in place of RFCOMM: URI formatting/parsing, rejection of other models and malformed addresses, a 32-byte status reply written in many small fragments returned whole by one read, a delayed reply, timeout versus EOF, and a short or failed write.
- [x] 3.2 Make the driver's live-status gates and status read transport-neutral, keeping the USB presence count USB-only and the socket test transport skipped; verify in `test_driver` that USB, Bluetooth and socket URIs each reach only their own operations, and that an unreachable Bluetooth device publishes offline without the "present but cannot be opened" error.
- [x] 3.3 Verify link loss: in `test_device_bt`, a closed socketpair peer makes the production status read and write fail; in `test_driver`, a Bluetooth queue whose write fails during raster, or whose status stream ends during finalization, fails the job, cleanup writes nothing further, recovery is required, and a following valid job emits no raster.
- [x] 3.4 When an address is configured, make discovery and queue creation use only the Bluetooth URI; verify with fake USB that an attached USB PT-P710BT receives no status request or output, and that the queue identity is still PT-P710BT.
- [x] 3.5 Verify byte identity: in `test_driver`, a two-label batch per finishing policy (chain-only submitted as its collection template) produces the same command and raster bytes on a USB and a Bluetooth queue, and the four policies produce distinct bytes; any difference fails the task.
- [x] 3.6 Run the full build and tests in the pinned container (`cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build --output-on-failure`) plus both existing image smoke variants; retain logs.

## 4. Image and deployment

- [x] 4.1 In `.github/workflows/deploy.yml`, set up QEMU, add a non-publishing `linux/arm64` build step beside the amd64 check build, and add `linux/arm64` to the publish step; verify on the pull request that the arm64 build passes all CTest suites under emulation, and record the duration. Done on PR #52, run 38634794603: 14/14 suites passed; the arm64 step took 14 min 50 s (amd64: 4 min 7 s).
- [x] 4.2 Write the Pi deployment guide in `docs/` (64-bit Raspberry Pi OS Lite, Docker, `bluetoothctl` pair and trust, disabling host `avahi-daemon`, `--network host` with no USB mount, the three variables, recommended polling `0`, about 10 m range, mid-job link loss recovery); review each command against the design and the container's entrypoint.
- [x] 4.3 Add `deploy/pi/` to build an unattended, power-loss-tolerant SD card (read-only overlay root, persistent `/data` for Docker, pairing, journal and configuration; `ptouch-update`, `ptouch-pair`, `ptouch-logs`), and document it in the Pi guide; verify on hardware. Issue #56. Verified on a Pi Zero 2 W: first boot, `/data` persistence and `/` reset across reboot, pairing, Bluetooth and service up after reboot, one 12 mm label printed over Bluetooth.

## 5. Physical acceptance

- [x] 5.1 Prepare a concrete bounded Bluetooth gate in `tests/` before any output: pairing check; a reset-free status reply on a fresh connection; one 12 mm default-trim label with receiving-ready time recorded; two jobs queued together sharing one connection, then closed when idle; a chain-only job followed after the queue went idle by a chain-and-final job; the measured connect failure time with the printer off; a job queued while the printer is off that prints after power-on; polling `0` showing no idle connects in the log; a USB-attached unit receiving nothing; and a Pi reboot followed by a label without re-pairing. State the label budget and stop conditions, and present the gate for approval.
- [ ] 5.2 After approval, run the gate on the Pi with the P710BT on battery; record captures, job logs and operator observations, stopping on the first failed criterion. If the held chain end is lost across the idle reconnect, stop and report it as a contract decision rather than adjusting behavior.
- [ ] 5.3 Update README capabilities and the acceptance record with the certified Bluetooth scope and anything left software-only.

## 6. Final reconciliation

- [ ] 6.1 Review implementation, tests, docs and evidence against both deltas, run strict OpenSpec validation, then sync specs and archive the change.
- [ ] 6.2 Open the pull request, confirm CI builds and tests both architectures without publishing, and merge only after 6.1.
