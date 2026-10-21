## ADDED Requirements

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
