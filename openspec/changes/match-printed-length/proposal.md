# Proposal

## Why

The user reports that a 36 mm `brother_12mm_item` label prints about 40 mm long on the PT-P710BT, although the previous printer behaved differently. The P710BT driver sends the full page raster plus a separate 14-dot margin at each end, a likely 3.95 mm addition that violates the existing physical-page-length contract.

## What Changes

- Make the requested length mean the complete physical label, including required end margins, within 180 dpi rounding. Apply this to explicit image media, PNG image-derived length, and submitted raster pages.
- Correct P710BT output by accounting for its required 14-dot end margins inside the page. Preserve content size and placement; replace only verified blank end rows with the printer's feed margins.
- Publish the P710BT's actual end margins and usable length range through standard media attributes, using the existing geometry and media paths.
- **BREAKING for incompatible P710BT jobs:** reject content in the non-imageable end margins, and requests shorter than the documented 24.5 mm minimum physical feed length, rather than cropping, shrinking, or printing a longer label. PT-2730 acceptance and output remain unchanged.
- Verify the reported 36 mm case and a second length through captured commands and reconstructed page pixels, including separated labels, joined strips, and held-run continuation. Keep current finishing and recovery policies. The user will validate physical output separately; this change has no physical acceptance check.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `label-rendering`: Define physical total length, safe replacement of blank end margins, P710BT fit rejection, and truthful model-specific media limits.
- `printer-device`: Distinguish logical page rows from transmitted P710BT print-area rows and account for feed margins exactly once.
- `server-architecture`: Extend canonical media and strict rendering to model-specific end margins and minimum physical length while retaining the existing adapter and batch lifecycle.

## Impact

Implementation is expected in `src/driver.c`, `src/filter_png.c`, and the existing geometry helpers/headers, with narrowly scoped changes to the PAPPL media-attribute bridge if needed for top/bottom margins. Extend the existing production-driver, PNG, raster, CUPS, and image-smoke checks; update `README.md`, `docs/ADDING-A-MODEL.md`, and `openspec/config.yaml`, and update `patches/README.md` only if the PAPPL media bridge changes. Record software validation in this change's `validation.md` using repository fixtures only, without modifying historical physical acceptance records or requiring live-template inspection.

No template-specific offset, client protocol, calibration setting, dependency upgrade, new spooler, or additional page buffer is proposed. Preserve PT-2730 byte-identical regressions. This proposal authorizes no implementation, physical printing, production configuration change, or rollout.
