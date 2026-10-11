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
