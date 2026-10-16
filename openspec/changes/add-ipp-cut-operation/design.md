# Design

## Context

See proposal.md for motivation. Ordinary FF completion leaves an open run; final chain-off/EJECT closes it. On 2026-10-16, the Pi's PT-P710BT over Bluetooth printed and cut a normal marked label successfully, but both chain-off/EJECT alone and a 239-byte initialized zero-raster page failed to release a held label. The operator then proposed a minimum blank continuation and confirmed that it released the previous marked label.

The successful 57-byte payload on 12 mm tape was:

```text
1b696101 1b692100 1b697a84000c00010000000100
1b694d40 1b694b00 1b69640e004d02
4711000f 00000000000000000000000000000000 0c
```

It selects raster mode and automatic status, declares one continuation row, enables auto-cut and chain mode, retains the 14-dot margin and PackBits compression, sends sixteen white bytes as one literal row, and ends with FF. It sends no invalidate or initialize. Status progressed through printing (type 6, phase 1), print-completed (type 1), and receiving-ready (type 6, phase 0), with zero error bits. Physical release was confirmed by the operator. The raster row is 0.141 mm at 180 dpi; total tape feed and blank tail length were not measured.

Evidence is recorded on the local `test/cut-only-probe` branch and can be read with `git show 9e239ae:tests/CUT-ONLY-PREP.md`; that file is not present in this proposal branch. The temporary raw capture is `/tmp/ptouch-cut-only-20261016-bt-evidence/blank-chain-label.jsonl`. During implementation, retain the result in the existing `tests/CUT-PROBE.md`, the single destination for task 2.3's evidence, rather than adding another report. The original Pi service was restored. The bytes above make this proposal independent of temporary files. Direct wire proof is distinct from the future IPP handler's software validation.

PAPPL 1.4.11 owns the queue and scheduling. Its public `papplPrinterOpenDevice` reserves maintenance ownership and returns NULL both for contention and an open failure. The operation callback receives unsupported printer operations. PAPPL's base `operations-supported` and driver attributes are copied separately, so adding another attribute alone would duplicate discovery.

## Goals / Non-Goals

**Goals:** Release held user content through one IPP action, use the tested minimum-raster continuation, and preserve safe subsequent printing and repeat requests.

**Non-Goals:** A raster-free cutter promise, PT-2730 cutter support, changes to ordinary finishing defaults, another spooler, a web UI, arbitrary raw commands, feed options, or further hardware experiments in this change.

## Decisions

### 1. One model-scoped printer operation

Use implementation-defined `Ptouch-Cut`, numeric `0x5f01`, at the configured queue URI. Use standard IPP charset, natural-language, target, and optional requesting-user attributes. Reject documents and job attributes before device output. Register the callback in `system_cb`; keep private state and handling in `driver.c` through `driver.h`. Other operations remain PAPPL's responsibility.

**User decision, 2026-10-16:** Ptouch-Cut uses the same printing authorization as Print-Job/Create-Job. Any client allowed to print may release its held label. This includes remote network clients when PAPPL has no authentication service or callback, as in the current Pi deployment. When authentication or a print group is configured, the same existing checks apply to printing and cutting. No new permission setting or administrator-only restriction is introduced.

PAPPL 1.4.11's public `papplClientIsAuthorized` checks administrator access and must not be used here: without authentication it rejects non-localhost clients. The actual printing check, `_papplPrinterIsAuthorized(client)`, is private. Add one new narrow PAPPL 1.4.11 patch file (named in `patches/README.md`) containing both the operation-advertisement correction and a public `bool papplPrinterIsAuthorized(pappl_client_t *client)` wrapper that delegates to that existing printing check, including its HTTP denial response. Apply it after the existing patches in both Dockerfiles. Declare it in the installed public header and have the application call it before device acquisition. Reuse the policy rather than copying its group, authentication or TLS logic, including private headers, or bypassing authorization.

Task 2.1's access coverage must include a request classified by PAPPL as a non-localhost network peer with authentication unset, proving that both Print-Job and Ptouch-Cut are allowed. It must also prove that a client denied by the configured printing policy cannot emit cut output. Localhost-only success cannot establish this behavior.

Advertise and accept the action only for PT-P710BT. PT-2730 returns `server-error-operation-not-supported` and keeps existing print bytes. The numeric value is a project-local vendor extension, not an IANA-assigned standard operation; standard print dialogs need not expose it. An empty job adds document processing to a direct device action, and an HTTP route would not provide the requested IPP interface.

### 2. Reuse the successful blank continuation

Build the sequence above with existing protocol builders and model/tape information. The only variable in the captured payload is the confirmed loaded tape width. Use a small pure protocol helper rather than a document renderer or a configurable page generator. Do not initialize, invalidate, send EJECT, or replay any user raster. Keep exactly one white row, the existing 14-dot margin, continuation page flag 1, auto-cut on, chain on, and FF. This is the chosen mechanism, not an automatic fallback after trying failed commands.

Send directly through the configured printer device; do not submit the internal page through IPP, filters, the job scheduler, or client media-length validation. Preserve current ordinary page geometry and the P710BT length correction in main commit `6e7fc07`. Tests must prove those ordinary print captures stay unchanged. USB and Bluetooth use the same sequence; only Bluetooth/12 mm currently has direct physical proof.

Zero rows would be simpler but failed physically. One literal white row reuses the production PackBits encoding without introducing an alternative zero-line opcode.

### 3. Keep physical continuation separate from held user content

Extend the existing run enum with one state, `PT_RUN_OPEN_BLANK`, rather than adding a separate flag or request history:

| State | Meaning | Ptouch-Cut behavior |
| --- | --- | --- |
| CLOSED | No known open run | Successful no-op after ownership |
| OPEN | Healthy run holding user content | Send the blank continuation |
| OPEN_BLANK | Healthy run after releasing user content, with only the blank continuation held | Successful no-op after ownership |
| UNCERTAIN | Partial or unconfirmed output | Refuse with the existing recovery instruction |

On confirmed action completion, set OPEN_BLANK, not CLOSED. Both open states count as continuation for the next real batch, so it uses normal continuation page settings and mandatory precut without initialization. FF for new user content sets OPEN again; a normal terminal cut sets CLOSED. Rejected or canceled jobs that emit no output preserve either healthy open state. Existing media invalidation clears either open state while preserving uncertainty. State remains process-local, as today.

CLOSED would falsely represent a physically open blank page and cause fresh-run initialization. OPEN would make every repeated request send another blank page. A fourth enum value provides both continuation and repeat no-op without two independently synchronized fields. The next batch may separate the remaining blank tail through its ordinary mandatory precut; do not promise zero total blank-tape consumption.

### 4. One exclusive action with existing failure handling

Acquire `papplPrinterOpenDevice` before inspecting or updating private run state. Keep ownership through live media confirmation, transfer, completion, and final state update. Use the existing state mutex, never holding it across PAPPL calls.

If OpenDevice returns NULL, return `server-error-busy` with a message that the printer is busy or unavailable. This deliberately covers both contention and inability to open: PAPPL does not distinguish them through this API. It avoids another ownership flag or a check-then-open race. Once ownership is acquired, unusable/error live status returns `server-error-device-error`. An uncertain run fails without output; CLOSED and OPEN_BLANK succeed without feed/cut bytes. There is no deferred action or automatic retry.

For OPEN, drain preceding notifications and confirm clean, supported loaded media without resetting the device. Reuse cassette/media invalidation and recheck state before output. If cassette invalidation changes the run to CLOSED, return a successful no-op without sending the blank continuation. Set UNCERTAIN immediately before the first page-setup write. Use existing checked transfers and bounded packet assembly, refactored to printer scope only where necessary. Wait for the current page's receiving-ready notification (type 6, phase bytes 0/0/0); print-completed alone is insufficient. On confirmed completion set OPEN_BLANK; on any partial output, status error, timeout, or link loss retain UNCERTAIN. A lost successful client response then needs no special deduplication record.

Explicit cuts are active user operations, exempt from `PTOUCH_IDLE_POLL_SECONDS`, including zero. Their reads neither consume nor reset the automatic idle-poll slot. The Bluetooth connection is owned only for the action and closed afterward under the existing device lifetime policy; USB fallback is forbidden.

### 5. Truthful discovery and bounded validation

Add a narrow PAPPL 1.4.11 correction that merges driver-provided additional operation enums into the existing `operations-supported` response once, preserving all standard operations and requested-attribute filtering, and exposes the printing-authorization wrapper from Decision 1. The driver contributes 0x5f01 only for P710BT. Apply the patch in both Dockerfiles and document its purpose/removal criteria in `patches/README.md`: remove each part when the pinned dependency supplies equivalent supported APIs. Do not clone PAPPL's operation list or create an extension registry.

Use existing protocol tests and the adapter's real IPP/socket-device fixtures. Cover exact white-row bytes, both transports, both held-content policies, CLOSED/OPEN_BLANK no-ops, a next real batch after release, a later cut after new content, and unchanged ordinary print captures. Exercise printing/polling/cut contention deterministically, failed opens, stale/fragmented statuses, malformed requests, access denial, media changes, and transfer/completion failures. Confirm one complete operation list, conditional model discovery, no new jobs, no saved raster, and explicit cuts with polling disabled/rate-limited.

Add one numeric `ipptool` request and README invocation, and retain the existing direct physical result with the failed candidates clearly distinguished. Run the documented request against the simulated server. Core and pinned adapter checks establish implementation behavior; today's direct P710BT Bluetooth observation establishes the physical mechanism. Do not turn the old unapproved nine-label/three-configuration plan into a prerequisite. Additional end-to-end hardware certification, if later requested, needs its own bounded procedure; it is not an unchecked task here.

## Risks / Trade-offs

- [One white row does not imply 0.141 mm total feed] → Document the 14-dot margin, unmeasured mechanical feed, and remaining blank tail; provide no feed-length option or waste-free claim.
- [Repeated requests could consume tape] → OPEN_BLANK makes repeats no-ops while preserving physical continuation.
- [A request races with printing or polling] → Use PAPPL ownership through the complete transition and test contention; do not queue the cut.
- [Output completion is ambiguous] → Preserve the existing uncertain-output recovery and never replay automatically.
- [Direct evidence is narrower than software support] → Document P710BT Bluetooth/12 mm physical proof separately from USB/other-width captures and integrated IPP checks.

## Migration Plan

Implement on the current main baseline, retaining its ordinary label-length and finishing behavior. No production configuration changes are needed. This proposal update does not resume the stopped loop or deploy a candidate. Removing the custom operation is the rollback; finish or recover a held run before restarting, under the existing recovery policy. The new state is not persisted.

## Cut pass

Removed: the disproved no-raster/no-blank-page restriction, claims of a terminal EJECT/closed physical run, unproven PT-2730 cutter support, the nine-label approval/acceptance gate, model-protocol exploration tasks, and a separate busy-versus-open-failure tracking mechanism. Reuse the current scheduler, transports, status parser, protocol builders, mutex, idle-poll slot, and recovery latch. Add no retry, queue, raw-command option, feed control, persistent request record, or generic maintenance framework.

| Item retained | Requirement or explicit request served | Simpler alternative and concrete failure |
| --- | --- | --- |
| Numeric operation and callback | Direct IPP cut action | No callback leaves no IPP device action. |
| Standard charset | Direct IPP cut action | Omission produces a malformed IPP request. |
| Standard natural language | Direct IPP cut action | Omission produces a malformed IPP request. |
| Target and job/document validation | Direct IPP cut action | Unchecked input can target the wrong queue or silently discard submitted print data. |
| Existing printing authorization through one public PAPPL wrapper | Direct IPP cut action; user's explicit printing-permissions decision | The available public admin check blocks the Pi's remote clients; copying private policy duplicates authentication logic. The wrapper delegates to the existing print check. |
| Non-localhost allow and configured print-policy denial checks | Direct IPP cut action; user's explicit printing-permissions decision | Localhost-only tests pass under either print or admin policy and miss remote clients being blocked. |
| P710BT-only discovery and rejection for other models | Cut support reflects physical evidence | Claiming both models promises an untested PT-2730 mechanism. |
| One merged operation list and patch in both images | Direct IPP cut action | Driver attributes alone duplicate discovery; patching one image gives inconsistent capabilities. |
| Existing exclusive device ownership | Direct cuts use exclusive device access | An idle precheck races with a scheduled job. |
| Single busy response for failed acquisition | Direct cuts use exclusive device access | Separate busy/open errors need ownership state the public API does not expose. |
| Existing mutex | Healthy continuation | Unlocked state updates race with status processing. |
| Existing live media check/invalidation | Direct cuts use exclusive device access | Cached media can preserve a held run across cassette changes. |
| One-row blank continuation helper | Direct IPP cut action; user's successful blank-label test | Zero rows failed physically; rendering a document adds a job/filter path. |
| Existing fixed margin and white literal encoding | Direct IPP cut action | Changing the tested sequence loses its physical evidence; new encodings duplicate existing builders. |
| OPEN_BLANK enum value | Healthy continuation | CLOSED causes initialization; OPEN repeats blank output. |
| Existing uncertainty and checked transfer handling | Cut action reuses printer ownership and recovery | Write success alone cannot establish safe completion. |
| Current-page bounded ready wait | Cut action reuses printer ownership and recovery | Stale ready or print-completed alone can falsely report success. |
| Explicit-operation idle-poll exemption | Idle status polling is configurable | A zero polling interval would otherwise disable the Pi's cut action. |
| Bluetooth action lifetime | PT-P710BT can use Bluetooth instead of USB | Job-only ownership cannot reconnect for an idle explicit action. |
| Exact-byte/model/transport tests | Cut support reflects physical evidence | Response-only checks miss wrong media fields or nonwhite pixels. |
| Repeat/continuation/failure/race tests | Healthy continuation; Direct cuts use exclusive device access | A single happy-path request misses tape-consuming repeats and interleaved output. |
| Numeric ipptool fixture and README | User's requested IPP interface | A symbolic project-only operation name is unknown to stock clients. |
| Existing physical record, retained under tests | Cut support reflects physical evidence | A socket response alone does not establish a physical cut. |
| Task 1.1: helper and exact-byte tests | Direct IPP cut action | Reusing standalone EJECT repeats a failed mechanism. |
| Task 1.2: run-state distinction and regression tests | Healthy continuation | The old three states cannot encode both blank continuation and repeat no-op. |
| Task 2.1: IPP handler and behavior tests | Direct IPP cut action; Direct cuts use exclusive device access | Protocol-only work supplies no callable action. |
| Task 2.2: discovery correction and checks | Direct IPP cut action | A handler alone does not advertise its capability truthfully. |
| Task 2.3: example and evidence documentation | User's IPP request; Cut support reflects physical evidence | Undocumented numeric bytes are not a convenient client interface. |
| Task 3.1: core/adapter/OpenSpec validation | Repository checks; user's proposal request | Unit success alone misses framework integration and malformed deltas. |
