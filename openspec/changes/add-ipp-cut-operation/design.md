# Design

## Context

See proposal.md for motivation. Current printing ends an unfinished batch by sending FF and recording `PT_RUN_OPEN`; `pt_print_pending` finalizes a current label by disabling chain mode before EJECT. That does not establish a working cut-only sequence after FF. `tests/CUT-PROBE.md`, under Earlier probes, records standalone EJECT attempts that did not release the label.

`src/main.c` builds one PAPPL system and configured queue. `src/driver.c` already owns checked device writes, transport-specific status reads, and a mutex-protected closed/open/uncertain run state. Status polling uses `papplPrinterOpenDevice` and `papplPrinterCloseDevice`. Inspection of pinned PAPPL 1.4.11 confirms that OpenDevice reserves maintenance access under the printer lock and returns NULL when a job or another device user owns the printer. The printer IPP dispatcher calls `papplSystemSetOperationCallback`'s callback for otherwise unsupported operations.

PAPPL creates `operations-supported` in its base printer attributes and copies driver attributes separately into responses. Merely adding the same attribute from the driver would duplicate it. A small dependency correction is needed for truthful discovery, in addition to the application handler.

## Goals / Non-Goals

**Goals:** One synchronous device action using existing ownership and recovery, with exact documented wire semantics and physical evidence for each supported model/transport.

**Non-Goals:** A maintenance command framework, another spooler, changing finishing defaults, a web button, automatic idle cutting, arbitrary raw command submission, or persistent command history.

## Decisions

### 1. One printer operation with no custom request attributes

Use project-local operation `Ptouch-Cut`, numeric `0x5f01`, at the existing queue URI. Requests carry the standard charset, natural language, printer URI, and optional requesting user, with no document data or job attributes. The operation has no feed-length, copy-count, or force option. Document its implementation-defined status: the numeric code is a local vendor extension, not an IANA-assigned standard operation. IPP reserves 0x4000–0x7fff for such extensions ([RFC 8011 sections 5.4.15 and 7.8](https://www.rfc-editor.org/rfc/rfc8011.html#section-7.8)).

Register the callback in `system_cb`; keep handling and access to private run state in `driver.c`, exposed through `driver.h`. Use PAPPL's request/response and configured authorization APIs. Unrelated operations remain PAPPL's responsibility. Reject document payloads and invalid targets before output.

Alternative: an empty print job with trim would add document/job processing to a direct printer action. A new HTTP route would not provide the requested IPP interface.

### 2. Publish one complete operation list

Declare the extra operation from the driver. Add a bounded PAPPL 1.4.11 patch that combines this additional enum with the framework's existing operations in a single `operations-supported` response, preserving every standard operation. Apply it in both Dockerfiles and document the correction and removal criterion in `patches/README.md`: remove it when the pinned dependency can advertise additional operations correctly through supported APIs. Do not replace or clone the framework's standard operation list in application code, and do not add a new extension registration framework.

Alternative: a second attribute of the same name gives ambiguous capabilities; a hardcoded copy of PAPPL's list drifts when the dependency changes.

### 3. Own the device before interpreting the run

Acquire the configured queue's device through `papplPrinterOpenDevice` and keep ownership through the final state update. Check the existing recovery latch and run state under that ownership. Reuse the current state mutex without holding it across PAPPL calls. Device contention returns `server-error-busy` immediately; an idle open failure or unusable live status returns `server-error-device-error`. A known closed run is a successful no-op after exclusive access is obtained. An uncertain run fails with the existing recovery instruction.

For a healthy open run, confirm clean live media without reset or initialization. Honor cassette invalidation from the existing status path and recheck run state afterward; do not cut an end the server no longer knows to be held. Set the existing state to uncertain immediately before the first output-changing cut command. Use checked writes and a bounded completion wait; set closed only on confirmed success. Repeated requests after success then do nothing. Failed or disconnected responses do not trigger retries.

Status reads must not let a prior page's completion satisfy the cut's completion wait. Preserve the existing draining of preceding notifications. Derive the cut's expected completion sequence from actual model behavior rather than assuming that print-completed alone, or the successful write itself, proves a cut.

Alternative: checking idle state before taking ownership races with a newly scheduled job; a new mutex alone would not serialize with PAPPL's scheduler. Queuing cuts could cut a different later label. Request identifiers and deduplication records are unnecessary because known closed state already handles repeats after success, and uncertain state blocks ambiguous retries.

### 4. Resolve the Brother sequence within the protocol boundary

Use primary Brother protocol documentation and the retained probe evidence to identify the model's feed/cut action after a completed chain FF. Any additional bytes belong in `protocol.c`, with pure builder coverage in `test_protocol`. The adapter uses the configured model and existing USB/Bluetooth connection; it sends no page header, raster, synthetic FF page, or initialization to make the cutter work. Reuse job transfer/readiness helpers at printer scope only where needed by the new action.

The concrete bytes and completion replies are implementation prerequisites to verify, not evidence already supplied by the proposal. If either model cannot cut its held end without a helper print, stop with the captured failure; do not silently change the contract or add a blank-label fallback. Model-specific command bytes, if required, stay within the existing model distinction.

### 5. Verify through existing fixtures and a bounded physical check

Extend `test_protocol` and `test_driver`; use the driver's existing IPP test server and simulated device for real numeric operation requests, emitted-byte checks, deterministic ownership races, status fragmentation, and injected failures. Cover both held policies, closed repeats, busy rejection, offline/fault/uncertain refusal, malformed requests, and failed transfer/completion. Check discovery returns one complete operation list and no job is created. Preserve existing print captures and finishing/default tests. P710BT cut bytes must match between USB and Bluetooth fixtures.

Add a numeric `ipptool` request file in `tests/` and a README invocation. Use the same request for integrated hardware acceptance and save results in one cut-only acceptance document in `tests/`.

Prepare the acceptance procedure before physical output and obtain approval for the concrete procedure. The integrated check uses 12 mm tape and three complete labels per configuration: PT-2730 USB, P710BT USB, and P710BT Bluetooth, nine labels total. Per configuration: print one chain-only label, wait until idle, cut and observe its release, repeat the cut and observe no feed, then print two labels with none and cut their joined strip. The second batch also checks fresh-run startup after a successful cut. On Bluetooth, let the idle connection close before cutting and verify there is no USB access. Record the exact commands, status sequence, physical separation, and measured extra feed; allow necessary cutter travel but no new blank label or replay. Stop at the first failure without automatic reprints. Additional diagnostic labels require a separately bounded procedure.

## Risks / Trade-offs

- [Standalone finalization has failed before] → Keep physical success mandatory and retain actual wire/status evidence; socket success is only software evidence.
- [A cut begins while another action starts] → Reserve the device through PAPPL and keep ownership through the state transition; deterministic contention tests must prove non-interleaving.
- [A cut response is lost after physical output] → Confirmed success leaves closed state; uncertain completion uses the existing recovery latch. Do not promise exactly-once behavior across restart or client resubmission.
- [Bluetooth reconnect changes the printer session] → Exercise the idle reconnect explicitly; fail the acceptance check if the held end cannot be released.

## Migration Plan

No configuration or client migration is required for ordinary printing. During apply, run the repository's core CMake/CTest checks and the full pinned adapter container build. Deploy only the isolated candidate needed for an authorized hardware check; use pfa's SSH connection to vault for any requested configuration changes there. Keep existing deployments available for restoration. Publish documentation of physical support only after the acceptance task passes. Rolling back removes the extra operation; finish or recover a held run before restarting either version, as required today.

## Cut pass

The final plan excludes empty jobs, helper labels, a separate HTTP endpoint, a command queue, automatic retries, request-deduplication storage, new run-state fields, feed-length/force options, a web UI, and a generic maintenance framework. None is needed for the requested direct IPP cut. No project code was changed in this proposal.

Each retained item below serves the named delta requirement or explicit workflow request; the final column identifies the simpler alternative and its concrete failure. Existing mechanisms are reused rather than duplicated.

| Item retained | Requirement or request served | Simpler alternative and failure |
| --- | --- | --- |
| One numeric printer operation and PAPPL callback | Direct IPP cut action | Omit it: clients cannot request the action over IPP. |
| Standard charset attribute | Direct IPP cut action, ordinary IPP request syntax | Omit it: malformed IPP request. |
| Standard natural-language attribute | Direct IPP cut action, ordinary IPP request syntax | Omit it: malformed IPP request. |
| Standard printer URI and target validation | Direct IPP cut action | Omit it: no valid queue target. |
| Existing configured access check | Direct IPP cut action | Bypass it: the extension can evade configured server access. |
| Reject document data | Direct IPP cut action | Accept it: an input promised to be document-free silently discards data. |
| Single operation-list merge and pinned patch | Direct IPP cut action, discovery scenario | Driver attribute alone: duplicate operations-supported; copied base list: dependency drift. |
| Both Dockerfile patch applications | Direct IPP cut action, discovery scenario | Patch one build: the other image advertises a different capability set. |
| Existing exclusive device ownership | Direct cuts use exclusive device access | Idle-state check alone races with a newly scheduled print. |
| Immediate busy/error response | Direct cuts use exclusive device access | Defer execution: a later label can be cut unexpectedly. |
| Existing clean-media refresh and cassette invalidation | Direct cuts use exclusive device access | Trust cached status: cut against a fault or an invalidated held run. |
| Existing run state, including closed no-op and uncertain latch | Healthy continuation; Direct cuts use exclusive device access | Unconditional cut/retry can feed again or act on uncertain output. |
| Existing state mutex | Cut action reuses printer ownership and recovery | Unsynchronized reads race with status updates. |
| Existing model selection | Cut-only support requires physical evidence | Assume identical model behavior: historical evidence does not establish that. |
| Protocol command builder | Cut action reuses printer ownership and recovery; existing Pure protocol core | Inline bytes in the adapter duplicate the protocol boundary. |
| Checked output transfer | Cut action reuses printer ownership and recovery | Buffered-write success can precede a failed device transfer. |
| Bounded, current-command completion check | Cut action reuses printer ownership and recovery | Successful write or a stale page notification cannot establish cut completion. |
| Bluetooth action connection lifetime | PT-P710BT can use Bluetooth instead of USB | Exclude explicit cuts from allowed lifetime: the action cannot reconnect after idle. |
| Exact-byte and transport-equivalence checks | Cut-only support requires physical evidence; PT-P710BT can use Bluetooth instead of USB | Response-only assertions miss new raster or transport-specific output. |
| Deterministic contention checks | Direct cuts use exclusive device access | Sequential tests do not establish exclusion during scheduler races. |
| Injected malformed/status/transfer/failure checks | Direct IPP cut action; Direct cuts use exclusive device access | Happy-path tests miss unintended output and unsafe replay. |
| Existing print regression checks | Healthy continuation; existing Explicit batch policies and Strict physical rendering | New-operation tests alone miss changed ordinary printing. |
| Numeric ipptool fixture and README invocation | Direct IPP cut action; user's request for an IPP interface | A symbolic custom name alone is unknown to stock clients. |
| One physical acceptance document | Cut-only support requires physical evidence | Software capture alone does not prove release; reuse one document for procedure and results. |
| Nine-label physical matrix and extra-feed observation | Cut-only support requires physical evidence | One model/transport or only single labels leaves another promised action unverified. |
| Existing core and full adapter builds | Repository's explicit validation instructions | Core-only checks do not compile or execute PAPPL handling. |
| Strict OpenSpec validation | User's openspec-propose request | File existence alone does not validate delta structure. |
| Task 1.1: protocol candidate, builder tests, and source notes | Direct IPP cut action; Cut-only support requires physical evidence | Reuse EJECT without investigation: prior standalone attempts failed. |
| Task 2.1: handler and behavioral tests | Direct IPP cut action; Direct cuts use exclusive device access | Builder-only work exposes no usable printer action. |
| Task 2.2: discovery correction and its tests/docs | Direct IPP cut action, discovery scenario | Callback alone leaves discovery incomplete. |
| Task 2.3: runnable client example and its check | Direct IPP cut action | Undocumented numeric extension is not practically callable by users. |
| Task 3.1: integrated software checks | Repository's explicit validation instructions | Separate unit successes miss pinned-library integration failures. |
| Task 3.2: concrete physical procedure and authorization | Cut-only support requires physical evidence; planning-only request | Unbounded or unapproved physical execution exceeds this proposal's authorization. |
| Task 3.3: physical execution and retained results | Cut-only support requires physical evidence | A prepared procedure is not evidence that the cutter works. |
