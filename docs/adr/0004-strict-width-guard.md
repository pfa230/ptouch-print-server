# 4. Strict tape and PNG content geometry

**Status:** Accepted

## Context

A client can select a page width that differs from the physical tape. A matching
page width also does not prove that a centered PNG fits the printable window:
PAPPL can clip source pixels while the old page-width guard accepts the job.

## Decision

Reject page/tape width mismatches before raster, using confirmed loaded media.
Use the shared `pt_tape_geometry` definition for publication and the canonical
printable window. There is no configurable width-recovery policy. Custom media accepts the
advertised ±0.01 mm rounding tolerance around a nominal tape width; the physical
window remains fixed and a different cassette still fails before raster.

At `print-scaling=none`, preflight the oriented PNG's physical content before
calling PAPPL's renderer. Accept an imageable-width design that fits the page.
Accept a nominal-width canvas only after verifying that every pixel outside the
physical window is white, then remove those blank margins before placement.
Reject oversized cross-tape content and content exceeding an explicit page's
length instead of silently clipping it. Preserve orientation and square DPI;
no DPI means 180 dpi. JPEG keeps PAPPL's standard placement path.

## Consequences

Wrong tape and oversized 1:1 PNGs fail visibly without page raster. Explicit
standard scaling remains distinct from a server-selected recovery policy.
Clients can use `media-ready` to design for the actual tape and margins.

## Alternatives considered

A server-selected fit/crop fallback would change the design's physical size or
lose content without the client's instruction. Keep strict rejection instead.
