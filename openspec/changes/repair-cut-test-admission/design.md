# Design

## Context

CI run 39232599310 built the adapter successfully, then driver_finishings_p710bt aborted at canceled_stream_behind_cut_case's active-job assertion; the other fourteen CTests passed. The fixture passes an empty pipe to cupsDoIORequest, waits for the streaming job to appear, and only supplies raster bytes after cancellation and cut completion. In pinned PAPPL, ipp_print_job calls _papplClientHaveDocumentData before create_job, and that function peeks one document byte. The test therefore waits for a job whose input it has prevented from arriving.

## Goals / Non-Goals

**Goals:** Establish the intended admitted-but-unfinished streaming request, then retain the cancellation/cut/completion order already required by the test.

**Non-Goals:** Production behavior changes, weaker assertions, longer admission timeouts, or new test scenarios.

## Decisions

Repair the fixture's upload coordination: send and flush sufficient document prefix for PAPPL to admit and recognize the streaming job, and keep the rest under test control until the canceled request is supposed to finish. Inspect CUPS buffering and PAPPL's streaming read sequence before choosing the prefix boundary. A single byte buffered only on the client is not proof of server admission; observe the actual job and streaming state as the existing assertions require. Preserve cancellation before ownership and delayed unowned completion after the cut is removed. Reuse the existing fixture and helper rather than adding a second regression suite.

The local core gate never executes the PAPPL adapter. Add a machine-local full-container check to the invoking worktree's loop.local.yaml, streaming git archive HEAD over pfa's existing SSH connection to vault and building a uniquely tagged check image. This uses the existing Docker daemon and does not attach printer devices or start a printer service. The same loop checks run after each implementation before review. The PR's ordinary CI remains responsible for the second architecture and both-model IPP smoke checks.

## Risks / Trade-offs

Client buffering or the server reading ahead can defeat an intended pause; use actual admission and input/completion coordination rather than sleeps. If the observed boundary cannot preserve this scenario, revise the helper within the same test to make that boundary explicit.

## Cut pass

- No spec delta: the existing cancellation and queued-ordering requirements already cover this case.
- Keep one fixture/helper correction: zero document bytes reproduces the observed admission failure; weakening the active-job assertion would stop testing the intended case.
- Keep the full-container gate: core-only checks passed while this adapter test failed in CI.
- Keep task 1.1: it repairs and demonstrates the reported test failure; no new independent scenario or production mechanism is added.
- Removed no additional mechanisms; the plan adds no timeout setting, fallback, alternate test suite, or production API.
