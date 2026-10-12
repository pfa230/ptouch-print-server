# Finishing-option verification

The PAPPL-only `driver_finishings` CTest target uses real PAPPL 1.4.11 objects
and IPP Create-Job requests. Its transport fixture injects raster, FF, and EJECT
write failures and fragmented printer status packets. It verifies that buffered
acceptance cannot hide those failures and that repeated cleanup emits nothing.

The same target uploads generated PNGs through IPP Send-Document, releases them
through PAPPL scheduling, and captures output using PAPPL's actual TCP device
scheme. Only the printer's online media/status callback is fixed in this test;
production status behavior is exercised separately with injected USB presence
and tape responses.

Verified scenarios:

- omitted finishings uses the advertised none default;
- none and trim enum choices and finishings-col templates reach the driver;
- three none copies end with three FFs and no EJECT or autocut;
- three trim copies end with two FFs and one EJECT;
- no-cut state survives a handle close/reopen and a gap beyond the old idle timer;
- canceled, malformed, and unsupported followers do not finalize a no-cut run;
- two jobs already Pending before printing both finalize when both request trim;
- legacy PTOUCH_CUT_MODE cannot override either explicit choice;
- partial status reads consume printing, print-completed, then receiving-ready;
- detected printer error, malformed status, and cancellation fail readiness;
- state exhaustion refuses a no-cut job before output;
- USB disappearance clears stale run/media state, and the existing refresh path
  consumes an old phase notification, then confirms 24 mm from the requested
  snapshot before creating the next page's nominal-width geometry;
- strict width mismatch and unsupported raw raster input remain rejected.

Run in the deploy build stage, which provides PAPPL/libusb/libpng:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The 2026-10-11 run passed all six container CTest suites. The host's seven
protocol-capture tests also passed. The complete deploy image built and its
localhost IPP smoke query reported supported finishings none/trim and default
none. These are software results, not certification of physical cuts or leaders.
PT-2730 validation remains recorded separately in CUT-PROBE.md and the OpenSpec
tasks, and integration waits for that gate.

The real-USB candidate test on 2026-10-11 completed three trim copies with zero
printer error bits and a receiving-ready notification consumed after each copy.
Physical output observations and the remaining hardware matrix are pending; see
CUT-PROBE.md.


On 2026-10-12 the observed precut-disabled three-copy test exposed joined final
two copies. A failing cutter regression and local correction keep autocut off
for the first copy and on for later copies, including the last. All six container
suites and seven host capture tests pass; the user observed all three corrected
copies cut apart, with leading tape attached to the first copy.

The CI smoke's terse ipptool command exited zero and printed PASS, but its grep
required a successful-ok string only printed in verbose mode. Its final log grep
could also fail before the container's log follower emitted startup lines. The
local workflow now uses the IPP test's exit status, prints a verbose final query,
and keeps container logs as diagnostics. The corrected shell block passed locally
with bash -e -o pipefail. The published workflow subsequently passed GitHub CI
on commit 688ffd4 (run 38278139208).

The 2026-10-12 hardware gate is now complete: the user observed explicit
none/none/trim with idle gaps, separated trim copies with precut enabled and
disabled, approximately one-inch frame length, and successful printing and
cutting of a queued trim job after switching the printer on. See CUT-PROBE.md
for the observations and logs. The final deploy image also built successfully
and passed the corrected workflow smoke locally. This validates publication
of the final correction.

Final review found that skipping all idle status polling would make cassette
width changes invisible until USB disconnection. A new regression failed
against that implementation. The local correction requests status without
ESC @ while a run remains open, both during idle polling and before creating
the next PNG job's geometry. Tests confirm same-width continuation, changed
width publication and chain clearing, and empty cassette detection, with only
the three-byte status request on the wire. All six CTest suites pass with the
correction. The full deploy image built and passed the corrected smoke. The
user confirmed the expected joined none/none strip and separately finalized
trim label after status queries and idle gaps, completing physical validation
before publication. The follow-up review reported no remaining actionable
defects.
