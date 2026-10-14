# Tasks

## 1. Configuration

- [ ] 1.1 Parse `PTOUCH_BT_ADDRESS` and `PTOUCH_IDLE_POLL_SECONDS` once at startup in `src/main.c`, failing on a malformed address, an address with any model other than PT-P710BT, or a polling value that is not a non-negative integer; verify each accepted and rejected case in a startup test, and that both unset leaves the USB queue URI and identity unchanged.
- [ ] 1.2 Document both variables and their defaults in README; check the text against the 1.1 test cases.

## 2. Idle polling control

- [ ] 2.1 Rate-limit `status_cb` by the parsed polling value (absent: unchanged; `0`: no device access; `N`: at most one live read per `N` seconds, keeping the last published status); verify in `test_driver` with a fake clock that unset matches current behavior, `0` opens no device across repeated calls, `N` reads once per window, and job-owned media refresh still runs with polling `0`.

## 3. Bluetooth transport

- [ ] 3.1 Add `src/device_bt.*` registering the `ptouch-bt://Brother/PT-P710BT/<address>` scheme with bounded connect, full write, bounded status read and close over an RFCOMM socket on channel 1; verify URI formatting/parsing and rejection of other models and malformed addresses in a new adapter test using a socketpair in place of RFCOMM.
- [ ] 3.2 Make the driver's live-status gates and status read transport-neutral, keeping the USB presence count USB-only and the socket test transport skipped; verify in `test_driver` that USB, Bluetooth and socket URIs each reach only their own operations, and that an unreachable Bluetooth device publishes offline without the "present but cannot be opened" error.
- [ ] 3.3 When an address is configured, make discovery and queue creation use only the Bluetooth URI; verify with fake USB that an attached USB PT-P710BT receives no status request or output, and that the queue identity is still PT-P710BT.
- [ ] 3.4 Verify byte identity: capture one PNG job per finishing policy over the socketpair transport and compare with the existing P710BT USB captures; any difference fails the task.
- [ ] 3.5 Run the full build and tests in the pinned container (`cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build --output-on-failure`) plus both existing image smoke variants; retain logs.

## 4. Image and deployment

- [ ] 4.1 Add `linux/arm64` to the publish step in `.github/workflows/deploy.yml` with QEMU set up; verify on the pull request that the arm64 build runs CTest and record its duration.
- [ ] 4.2 Write the Pi deployment guide in `docs/` (64-bit Raspberry Pi OS Lite, Docker, `bluetoothctl` pair and trust, disabling host `avahi-daemon`, `--network host`, the three variables, recommended polling `0`, about 10 m range, mid-job link loss recovery); review each command against the design and the container's entrypoint.

## 5. Physical acceptance

- [ ] 5.1 Prepare a concrete bounded Bluetooth gate in `tests/` before any output: pairing check, one 12 mm default-trim label, a chain-only two-label job followed after 10 seconds by a chain-and-final job, a job queued while the printer is off that prints after power-on, polling `0` showing no idle connects in the log, and a USB-attached unit receiving nothing; state the label budget and stop conditions, and present the gate for approval.
- [ ] 5.2 After approval, run the gate on the Pi with the P710BT on battery; record captures, job logs and operator observations, stopping on the first failed criterion. If the held chain end is lost across the Bluetooth reconnect, stop and report it as a contract decision rather than adjusting behavior.
- [ ] 5.3 Update README capabilities and the acceptance record with the certified Bluetooth scope and anything left software-only.

## 6. Final reconciliation

- [ ] 6.1 Review implementation, tests, docs and evidence against both deltas, run strict OpenSpec validation, then sync specs and archive the change before merging.
