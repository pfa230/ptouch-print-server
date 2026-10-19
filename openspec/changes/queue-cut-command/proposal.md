# Proposal

## Why

A client can receive print acceptance and request a cut while physical printing is still running. The Pi log shows Ptouch-Cut refusing ownership at 2026-10-19 04:07:26 UTC, four seconds before job 1 completed; the client should be able to place that cut after its accepted prints.

Fixes #65

## What Changes

- Queue Ptouch-Cut behind already accepted print jobs and earlier cuts, before later print jobs and cuts.
- Keep the request open until its turn and return the actual cut result. Temporary ownership contention no longer fails the request; a transport that cannot open at execution still returns an error.
- Integrate waiting device acquisition with PAPPL's existing scheduling and ownership, retaining the current cut protocol and recovery rules.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `printer-device`: Order explicit cuts with accepted print work rather than refusing device contention.

## Impact

The PAPPL 1.4.11 printer-operation patch, its documentation, the adapter's acquisition call, and existing cut tests change. The same IPP operation, authorization, request attributes, synchronous completion response, and blank-continuation bytes remain in use. Labeler and its MCP adapter need no interface change. No implementation or live deployment is part of this proposal.
