# job-history Specification

## Purpose
Let someone at the printer's web interface see what happened to recent print jobs, including why each failed one did not print, without reading container logs or querying IPP.

## Requirements

### Requirement: Landing page lists recent jobs

The web interface's landing page `/` SHALL list the queue's 20 most recent jobs, newest first, with job number, name, owner, pages, and status. With no jobs, it SHALL say so.

#### Scenario: Jobs after printing

- **WHEN** five jobs completed and one aborted since the server started
- **THEN** `/` lists all six, newest first, with the aborted job's status

### Requirement: Failed jobs show their reason

Every job status cell, on `/`, the printer page, and the Jobs page, SHALL show the job's `job-state-message` whenever one is set. An aborted or canceled job without a message SHALL show its `job-state-reasons` keywords instead.

#### Scenario: Wrong tape

- **WHEN** a job is aborted because its page does not match the loaded 18 mm tape
- **THEN** its row shows the aborted state, its time, and "Page geometry does not match loaded 18 mm tape at 180 dpi"

#### Scenario: Abort without a message

- **WHEN** PAPPL aborts a job without setting a message
- **THEN** its row shows the job-state-reasons keywords, such as `aborted-by-system`

### Requirement: Driver failures carry a specific message

A job that fails because a printer write did not complete, or because its PNG cannot be decoded, is too large, or has non-square resolution, SHALL have a `job-state-message` naming that cause.

#### Scenario: Corrupt PNG

- **WHEN** a submitted PNG fails to decode
- **THEN** the aborted job's message states that the PNG could not be decoded and includes the decoder's error text

#### Scenario: USB write fails mid-label

- **WHEN** a raster write to the printer does not complete
- **THEN** the aborted job's message states that the write to the printer failed and that output is uncertain

#### Scenario: Tape check cannot reach the printer

- **WHEN** the status request sent before a job's first page does not complete
- **THEN** the aborted job's message states that the status request failed and that nothing was printed

### Requirement: Saved job history survives restart

With a persistent state directory, the server SHALL restore saved job history after server restart, container replacement, and Pi reboot, within its existing retained-job limit. Saved job numbers, names, owners, timestamps, page counts, terminal states, reasons, and failure messages SHALL survive. Supplied container and Pi deployments SHALL keep this state outside the replaceable container.

#### Scenario: History after container replacement

- **WHEN** completed, aborted, and canceled jobs have been saved and the deployed container is replaced using the same persistent storage
- **THEN** the configured queue retains those jobs with their saved metadata and diagnostic text, and the existing history pages display them

### Requirement: Interrupted jobs return as history

A job saved in a nonterminal state SHALL return after restart as aborted history with a message explaining the interruption and need for explicit resubmission, rather than as an active job.

#### Scenario: Restart with unfinished input

- **WHEN** the server restarts with a saved unfinished job, including when its document and job-attribute files remain available
- **THEN** that job appears in aborted history with its interruption message and is absent from the active queue

### Requirement: Saved history belongs to the configured printer

If saved history belongs to a model or transport URI different from the configured printer, startup SHALL fail with an explanation identifying the conflict and the fresh-state-directory recovery, preserving the saved history rather than silently discarding or retargeting it.

#### Scenario: Intentional printer change

- **WHEN** a different printer is configured while the selected persistent directory still contains the preceding printer's state
- **THEN** startup reports the conflict, identifies the fresh-state-directory recovery, and preserves that state
