# Third-Party Notices

`ptouch-print-server` is distributed under the GNU General Public License v3.0
only (`GPL-3.0-only`, see [LICENSE](LICENSE)). It links and bundles the
third-party components below; each remains under its own license.

## PAPPL

The Printer Application Framework that provides the IPP Everywhere server,
driver model, and raster pipeline. Upstream:
https://github.com/michaelrsweet/pappl. Licensed under Apache-2.0.

## libusb

Used for raw USB I/O to the P-touch device through the custom `ptouch://`
scheme. Upstream: https://github.com/libusb/libusb. Licensed under LGPL-2.1.

## CUPS libraries

`libcups` / `libcupsimage` provide the IPP, raster, and option-parsing
primitives that PAPPL builds on. Upstream: https://github.com/OpenPrinting/cups.
Licensed under Apache-2.0.

## ptouch-print (protocol and tape tables)

The Brother P-touch raster protocol commands and the device/tape tables are
derived from hannesweisbach/ptouch-print. Upstream:
https://github.com/hannesweisbach/ptouch-print. Licensed under GPL-3.0. This
lineage is why the project as a whole is GPL-3.0.

## ptouch-print-macOS (cutter logic)

The cutter and pre-cut planning logic is derived from the ptouch-print-macOS
fork of ptouch-print. Licensed under GPL-3.0.
