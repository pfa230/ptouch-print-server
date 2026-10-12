# Tasks

## 1. Diagnostic evidence

- [x] 1.1 Add and document test-only cutter probes with checked transfers and protocol-aware captures; preserve the failed separate-finalization evidence and the successful single-label baseline observation in tests/CUT-PROBE.md.
- [x] 1.2 Verify the corrected receiving-ready handling on the PT-2730 using the probe or actual IPP driver, record raw statuses and physical multi-label output, and distinguish this flow-control result from the superseded queue-drain proposal.

## 2. Explicit finishing implementation

- [x] 2.1 Resolve each/none cutting from PAPPL job finishings, remove queue scans, replanning, idle timer and automatic abort/endjob ejects; verify driver tests cover enum/collection options, omitted default, queue independence, idle gaps, copies, and canceled or malformed followers.
- [x] 2.2 Preserve open-run startup across explicit no-cut jobs, suppress destructive idle status resets, fail an untrackable no-cut run before output, and check write/flush metrics and USB receiving-ready; verify injected raster/FF/EJECT failures and partial/error/readiness status packets without replay or duplicate cleanup output.
- [x] 2.3 Update README and ADR-0008 with finishing examples, none default, obsolete PTOUCH_CUT_MODE, and the lack of automatic finalization; review their claims against the passing driver and IPP captures.

## 3. Integration and hardware gates

- [x] 3.1 Run the core CMake/CTest gates, the full pinned PAPPL deploy build, and real IPP/socket checks for none/trim/default/collection/copies; verify unsupported finishings and existing strict-width/raw-format behavior remain correct, and run strict OpenSpec validation.
- [x] 3.2 Verify explicit none jobs followed by trim on the PT-2730, including idle gaps, copies, precut on/off, actual final cuts, leaders, measured lengths, no extra blank label, and offline recovery; leave this gate open if only software evidence exists.
- [x] 3.3 Sync specifications and archive before merging; verify accepted specs/archive and then follow the repository's PR convention.
- [ ] 3.4 Deploy only when rollout is authorized through pfa's SSH connection to vault; verify deployed revision and physical output and retain the prior server image for rollback.

Integration evidence: all main requirements and scenarios match the archived
delta, including its Purpose; strict main-spec validation passed. The archive
preserves .openspec.yaml. Final implementation commit 90a80a4 passed GitHub CI
(run 38278975501), and PR #45 was marked ready after that result. Production
rollout remains pending authorization and its own output check.
