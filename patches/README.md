# Pinned dependency corrections

Both image builds pin upstream PAPPL to **v1.4.11** and apply
`pappl-1.4.11-raster.patch` with `patch --batch --forward --fuzz=0 -p1` before compilation.
A changed dependency or a patch conflict must fail the image build.

Upstream: https://github.com/michaelrsweet/pappl/tree/v1.4.11
The modified upstream files retain their Apache-2.0 copyright/license headers.

The raster correction addresses three demonstrated boundary defects:

- Streamed geometry was copied only for input and output with at least eight
  bits per pixel. A forced one-bit driver consequently received its default
  media geometry. Preserve each submitted page while retaining the selected
  one-bit conversion format and correctly sized buffers; reject contradictory
  explicit media, including collection media-size-name, before the page
  callback. Match PAPPL's resolved-name precedence when the collection also
  supplies dimensions. Unsupported conversion must fail.
- A short input row was treated as an ordinary loop exit, after which a discard
  loop hid the error and completed the page. Require each declared row in full,
  with no default-page padding or discard path, and check every write callback.
- Image abort cleanup could call end-job while the job still appeared successful,
  or call it twice after end-job failure. Publish aborted state before cleanup
  and invoke that callback once. Stream cleanup likewise sees the failure state.

This patch adds no raster decoder or page spooler. CUPS retains raster decoding,
PAPPL retains its existing dithering, and the driver owns physical finishing.
The separate finishing patch below handles advertising, acceptance, and
synchronized defaults. Neither patch implements Brother command policy.

Remove these changes only when a pinned upstream release passes the same real
IPP/socket regression tests without the patch. A version upgrade alone is not
proof of equivalent behavior.

## Bounded CUPS decoder correction

Both images rebuild only `libcups` and `libcupsimage` from authenticated Debian
source version **2.4.2-3+deb12u9**, applying
`cups-2.4.2-deb12u9-raster.patch` through the shared
`scripts/build-cups.sh` helper. This retains Debian's full security patch set;
it does not replace the libraries with pristine upstream 2.4.2. The pinned
source must be refreshed when Debian publishes a replacement security revision,
with exact patch application and the raster regressions reverified. Build and
runtime guards reject a different Debian revision, preventing a cached pinned
library from silently replacing newer security fixes. The runtime records its
source version in `/usr/local/share/ptouch/cups-source-version`.

Debian source: https://sources.debian.org/src/cups/2.4.2-3%2Bdeb12u9/
Upstream decoder: https://github.com/OpenPrinting/cups/blob/v2.4.2/cups/raster-stream.c
The source retains its Apache-2.0 license and copyright headers.

CUPS returned zero for both clean end-of-input and a truncated or invalid next
page header. Its compressed-row decoder can read ahead into a private buffer,
so HTTP byte counting cannot distinguish those cases outside CUPS. A small
header-read helper probes **one byte per header**, then reads the remainder
normally. An absent first byte remains normal EOF; a missing remainder records
an error through the existing `cupsRasterErrorString` API. Existing header
validation failures now record an error too, without changing their acceptance
criteria. Negative input callbacks also record failure, preserving zero-byte EOF
as the normal completion case. PAPPL checks that error before its end-job cleanup.

No public API, ABI, raster parser, page spooler, scheduler, or finishing engine
is added. The helper preserves GnuTLS, Kerberos, Avahi, and Debian hardening
configuration. Only the ABI-compatible libraries are installed under
`/usr/local/lib`; Debian headers and command-line tools remain in use. The deploy
stage copies both patched libraries with PAPPL and runs `ldconfig`, so PNG/JPEG
and streamed raster decoding use the same patched runtime dependency.

Remove the CUPS patch only when the pinned dependency distinguishes clean EOF,
partial headers, and invalid headers through the existing error API and passes
all regressions without it. Keep the PAPPL error propagation until the pinned
PAPPL release supplies equivalent verified behavior.

## Bounded PAPPL finishing and media bridge

Both image builds also apply `pappl-1.4.11-finishings.patch` after the raster
patch, with the same strict patch flags. PAPPL's legacy finishing bitmask cannot
distinguish trim-after-pages (60), trim-after-job (63), and a registered collection
template. Its generated attributes also duplicate driver declarations and use
one printer-wide margin for every roll size. A conventional CUPS queue therefore
submitted 12 mm media with 53/53 margins, instead of its physical 63/71 margins,
and correctly triggered the driver's printable-window rejection.

The bridge activates only for drivers declaring both standard trim enums and
`finishings-col-database`. It replaces the explicit finishing declarations and
four canonical media attributes: `media-col-database`, `media-size-supported`,
`media-left-margin-supported`, and `media-right-margin-supported`. The driver
supplies each supported fixed tape width, variable length, and physical margins;
the bridge neither invents geometry nor relaxes raster validation. Legacy drivers
retain their existing generated attributes and finishing acceptance.

Job validation checks raw standard enums or one advertised collection template,
rejects conflicting representations and unsupported values, and accepts collection
no-value for none. Raw job attributes remain available to the driver. Raster
copies are restricted to the already advertised one, while images retain up to
999 copies. The single printer default is kept synchronized across enum and
collection representations, IPP updates, the existing public default setter, and
PAPPL state save/load. A template without an enum equivalent cannot become the
canonical default. Invalid updates fail before changing defaults. The legacy Web
UI form preserves this default when saving unrelated fields, since its finishing
checkboxes cannot represent these presets.

This is an internal bridge, with no public API/ABI change, new job attribute,
private finishing enum, command planner, or general finishing engine. Physical
cut policy and the implementation-defined `ptouch-chain` meaning belong to the
driver. Remove this patch only when a pinned upstream version passes the same
IPP/default/state and conventional CUPS regressions without it; a dependency
upgrade must revalidate both patches and their exact application.

## PAPPL job history and failure reasons

Both image builds apply `pappl-1.4.11-job-history.patch` after the finishing
patch, with the same strict patch flags. It changes only `pappl/printer-webif.c`.
In multi-queue mode the stock landing page `/` shows each printer's status card
and active-job count but no jobs, and no page renders a job's
`job-state-message`. A failed job therefore appeared only as "Aborted <time>",
and its reason was reachable only through IPP Get-Job-Attributes.

The patch lists the printer's 20 most recent jobs on `/`, newest first, through
the existing job row callback, so `/`, the printer page, and its Jobs page share
one row format. That callback appends the job's `job-state-message` to the
status cell, or, for an aborted or canceled job without one, its
`job-state-reasons` keywords. When no reason bit is set, it shows the keyword
IPP reports in that case, `aborted-by-system` or `job-canceled-by-user`. Text is
HTML-escaped by the existing client printer. No IPP behavior, public API, or ABI
changes. The deploy-image smoke checks that, on `/`, its malformed-PNG job's row
shows the decode failure and a job canceled before receiving data shows
`job-canceled-by-user`.

Remove this patch when a pinned upstream release lists jobs on the multi-queue
landing page and renders `job-state-message`.

## PAPPL printer operations and printing authorization

Both image builds apply `pappl-1.4.11-printer-operations.patch` after the job-history patch, with the same strict patch flags. It changes `pappl/printer-ipp.c` and the installed public header `pappl/printer.h`. Its two independent parts serve the project-local Ptouch-Cut operation (`0x5f01`):

- **Operation advertisement.** PAPPL copies its standard `operations-supported` and the driver attributes separately, so a driver-supplied value produced a second, partial attribute. When the driver declares `operations-supported`, the patch merges those enums into the printer's standard attribute once and removes the driver copy. Requested-attribute filtering is unchanged, and Get-Printers merges each printer's value into its own attribute. The driver declares `0x5f01` for PT-P710BT only; other drivers are unaffected.
- **Printing authorization.** PAPPL's public `papplClientIsAuthorized` checks administrator access and rejects non-localhost clients when no authentication is configured. The patch adds public `bool papplPrinterIsAuthorized(pappl_client_t *client)`, which delegates to PAPPL's existing private Print-Job/Create-Job check `_papplPrinterIsAuthorized`, including its HTTP denial response. Ptouch-Cut calls it before acquiring the device. No authentication, group, or TLS policy is copied.

The application links against the added public symbol, so both images must apply the patch. `test_driver` verifies one complete operation list with and without requested attributes, model-scoped advertisement, unauthenticated Print-Job and Ptouch-Cut from a non-localhost peer, and denial of both by a configured authorization callback before device access.

Remove the advertisement part when a pinned upstream release merges driver-provided operations into `operations-supported`. Remove the authorization part when a pinned upstream release provides a supported public API applying the printer's printing authorization to a client request.

## Verification

`test_pappl_raster` submits real IPP jobs to a minimal PAPPL driver and captures
converted raster at a TCP sink, with no USB access. It verifies 1-bit and 8-bit
PWG geometry/content, different page lengths with unknown total counts, 8-bit
URF, short rows, explicit-media contradictions, malformed/unsupported resolution,
unsupported RGB conversion, immediate write failure, and image failure cleanup.

Malformed termination is part of the default CTest suite: one-byte and longer
partial next headers, invalid complete next headers, truncated first headers,
and truncated second-page compressed rows are tested for PWG and URF. Failed
jobs reach aborted cleanup without normal end-job finalization; already-completed
pages are not replayed. Valid unknown-count jobs following failures still work.
Direct CUPS I/O fixtures additionally fail the input callback after a complete
page for compressed PWG/URF and uncompressed CUPS raster.
The former optional failing `--partial-next-header` probe is now ordinary required
coverage.

All patches use `--batch --forward --fuzz=0`: clean pinned application must
succeed; duplicate application and changed context must fail. Container build
and runtime library-resolution checks establish which libraries actually run;
command captures establish software behavior, not physical Brother certification.

`test_pappl_finishings` verifies declared enums/templates, unambiguous attributes,
atomic and synchronized IPP/public defaults, state roundtrips, image/raster copy
limits, and legacy driver behavior. Production driver captures check the actual
four cut policies independently. The deploy-image smoke uses real IPP and a socket
sink; ordinary CUPS integration also verifies the canonical media metadata. These
software checks do not substitute for the separately recorded PT-2730 hardware
gates.

The application initializes PAPPL's optional HTML footer to an empty string through
`papplSystemSetFooterHTML`. With v1.4.11 the default NULL footer crashes the existing
Printing Defaults renderer in localization; both the old and new app reproduce
this. The public-API initialization avoids extending the dependency patch and is
covered by the ordinary CUPS/Web UI fixture.
