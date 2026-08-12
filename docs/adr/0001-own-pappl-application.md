# 1. Build our own PAPPL printer application

**Status:** Accepted

## Context

The goal is to expose a Brother P-touch label printer as a driverless IPP Everywhere device so any
client can discover and print to it. Two credible routes existed: extend LPrint (which already
supports label printers, including some Brother models) or build a dedicated PAPPL application.

This was re-examined mid-project, after the protocol core already worked, precisely because "just use
LPrint" is the obvious question a reviewer would ask.

## Decision

Build our own PAPPL application.

## Consequences

- Full control over the raster path, which turned out to matter repeatedly: per-line flushing to
  avoid overrunning the PT-2730 buffer, a custom USB scheme, our own PNG filter, and cutter
  sequencing. Several of these are not expressible as LPrint configuration.
- We own the maintenance burden, including tracking PAPPL releases.
- The pure protocol core (`src/protocol.c`, `src/tables.c`, `src/raster.c`, `src/cutter.c`) is
  independent of PAPPL and unit-tested without hardware.

## Alternatives considered

- **Extend LPrint.** Rejected: adding a model means fitting its driver model, and the behaviours we
  needed (cut/chain planning, PNG-derived page length, custom device scheme) sit outside it.
