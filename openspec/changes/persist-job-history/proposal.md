# Proposal

## Why

The Pi keeps diagnostic logs but discards the application's job history whenever its container is replaced, leaving the web interface at "No jobs in history." Startup also creates the queue before PAPPL restores saved printers, which causes a duplicate and drops the saved history; PAPPL's state format additionally omits the failure messages the interface displays.

## What Changes

- Persist PAPPL job metadata in the deployed container and mount that state on the Pi's existing persistent data partition, so retained jobs and their failure explanations survive server restarts, container updates, and Pi reboots.
- Restore saved state before creating the configured queue, retaining exactly one queue and the existing model/transport selection rules.
- Explain and preserve conflicting saved history on an intentional printer change, with a documented fresh-directory recovery and accurate Pi pairing-helper output.
- Preserve job-state-message alongside PAPPL's existing job metadata and retention limit, using its state format rather than a second history database; correct the existing loader's rejection of Job records without `format`, including jobs canceled before receiving data and unfinished jobs awaiting data.
- Restore unfinished jobs as aborted history requiring explicit resubmission, without restoring printer run state or automatically replaying output.
- Update the supplied container deployment examples and Pi upgrade instructions, distinguishing web history from the already persistent journal. Existing lost history cannot be reconstructed from journal text.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `job-history`: Retain saved job metadata and failure explanations across restarts and deployed container replacement/reboot; show interrupted jobs as aborted history.
- `server-architecture`: Replace the blanket queue-persistence disclaimer with a distinction between persistent history and process-local printer run state.

## Impact

Changes are expected in `src/main.c`, the existing pinned PAPPL job-history correction, `patches/README.md`, pinned-PAPPL restoration fixtures under `tests/` and their registration in `CMakeLists.txt` if added as a new file, `docker-entrypoint.sh`, `deploy/pi/user-data`, `README.md`, `docs/PI-DEPLOYMENT.md`, and the existing deploy-image smoke. Reuse PAPPL state save/load, bounded retention, job rendering, and its existing spool-directory option. No printer protocol change, journal redesign, external database, history importer, retention setting, or live deployment belongs to this proposal. Both supported models use the same application history path.
