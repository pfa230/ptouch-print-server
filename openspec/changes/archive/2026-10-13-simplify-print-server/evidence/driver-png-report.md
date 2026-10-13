Scoped review fixes, 2026-10-13

Changed production paths: src/driver.c and src/filter_png.c.
Changed regression paths: tests/test_driver.c and tests/test_png_geometry.c.
No printing, remote access, deployment, or dependency changes.

Reproductions:
- before.log: collection media-size-name=om_12x50mm_12x50mm produced180 rows instead of354.
- before-driver.log: real scheduled USB-fixture PNG job polled status while processing; polling changed printer reasons. The run is intentionally UNCERTAIN during partial output, so the old recovery check published OTHER before reaching device acquisition.

Fixes:
- PNG explicit-length detection recognizes resolved media-col.media-size-name, matching PAPPL's name precedence.
- Status callback skips idle polling/recovery checks during processing and rechecks processing when OpenDevice returns NULL after an idle-to-active scheduling race. Idle present-but-unclaimable USB regression still requires OFFLINE/unconfirmed.
- Bounded custom-width support advertises only canonical±1 centimm in five existing variable-length size ranges. Named preset sizes remain exact. PNG tape lookup rounds millimeters; noncanonical raster width is permitted only when matching the explicitly selected ±1 dimensions and retaining the physical printable window. Explicit margins remain unchanged. The page span uses the actual header width, and PNG rejection reports the selected printable limit (42dots for the1199centimm/297margin case).

Evidence:
- Actual CUPS headers:1198/1199 centimm ->84dots,1200/1201/1202 ->85dots; all11bytes/row.
- Named50mm collection produces354rows; named25mm too short and named18mm wrong tape abort with no raster/finishing commands.
- Custom1199/1201 x3986centimm with explicit297 margins and42dot PNG produces282rows.76dot PNG rejects with no raster/finishing commands.
- Validate-Job with fidelity=true: canonical±1accepted,±2unsupported, for6/9/12/18/24mm.
- Active-job poll preserves media confirmation and reasons and emits no status reads; job completes. Existing idle USB claim-failure, unsupported width, malformed raster, canceled/faulted run,65/999copy and PNG geometry cases remain covered.

Verification command inside ptouch-named-media-build:20261013 isolated with network none and repository read-only:
cmake -S /src -B /results/final -DCMAKE_BUILD_TYPE=Release
cmake --build /results/final --target test_driver test_png_geometry -j4
ctest --test-dir /results/final -R 'driver_finishings|png_geometry' --output-on-failure -j2

Result:2/2passed; driver_finishings29.50s,png_geometry29.50s,total29.51s. build-final.log contains no compiler warnings. final.log is the authoritative final output.
