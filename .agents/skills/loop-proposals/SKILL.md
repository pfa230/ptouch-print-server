---
name: loop-proposals
description: Show the active OpenSpec proposals in the selected root, grouped and ordered for choosing what to take next. Use after batch scoping or when the user asks to list proposed changes or proposed issues.
---

# Show the proposed backlog

Present a read-only overview of the active proposals so the user can choose what to take first. This lists changes; it does not edit proposals, select or start a run, fetch branches or read all open issues.

## Discover the proposals

1. Run `openspec context --json` and `openspec list --json` from the invoking checkout. If the user names a store, discover it with `openspec store list --json` and retain `--store <id>` on the commands that accept it. Honor the resolved root, including a configured store pointer. Show the resolved root with the listing. If discovery fails or no root resolves, report it and stop without initializing anything.
2. Use exactly the active changes the native listing returns, without `--all` or `--archived`. Do not scan sibling worktrees, fetch branches or filter by GitHub issue state or loop enrollment. This works inside an unfinished scoping session and, after its proposals land, from the default checkout.
3. Under the resolved changes directory, read each change's `proposal.md` and `.openspec.yaml`. Retain a change whose files are missing or incomplete, identifying the unavailable fields rather than excluding it. If there are no active changes, say so and omit the order section.

## Enrich recorded source issues only

- Read the `Source issue:` URL recorded in each proposal, retaining its repository identity for cross-repository links. Never start from all open issues: the proposal files define membership, including proposals for issues that have since closed.
- Read referenced issue metadata with `gh issue view <URL> --json number,title,labels,assignees`, and each referenced repository's label descriptions with `gh label list` to separate kind, area and priority. Reuse repository issue-listing instructions where they apply.
- Use the issue title for a linked row and a short summary from the proposal text otherwise; use `.openspec.yaml`'s `created` date, or `-` when unavailable. An unlinked proposal shows `-` in the source column; a draft without a proposal shows `draft` as its summary.
- Keep one row per change even when several proposals share a source issue. If GitHub metadata cannot be read, report that limit, preserve the recorded issue link and explicitly mark any classification inferred from available proposal text. Do not invent labels, issue numbers or dates.

## Present the groups

State the derived kind and area axes before the listing. Put bugs under `Bug` first, then group the remaining proposals by repository area. When several areas match, choose the rarer one in the listed set and show the secondary area inline. Infer a classification from the proposal text when needed and mark it with `~`; use `Other` only when no area can be established. Sort area groups by descending count with `Other` last, and rows newest first with unavailable dates last. Keep titles to about 90 characters on one line. Brackets carry the secondary area, priority and assignee when available.

Each row has the change name, proposal date, source issue link when recorded, and the title or summary. Link a source issue reference to its recorded URL. For example:

```text
## Bug (1)
fix-worker-exit  2026-10-19  [#42](https://github.com/owner/repo/issues/42)  Preserve the worker's exit status  [adapters]

## Planning (1)
clarify-proposal-order  2026-10-18  -  Explain proposal ordering ~

## Order
1. fix-worker-exit: restores reliable failure reporting before further adapter work.
2. clarify-proposal-order: small documentation change with no prerequisite.
```

## Recommend an order

Recommend up to eight changes, fewer when appropriate, with one reason each. Rank by proposal dependencies first, then correctness, then small settled work, keeping related areas together where practical. Read a proposal's design or tasks when the proposal alone does not establish dependencies or scope, and state uncertainty rather than inventing a dependency. Missing proposal content is not a settled implementation candidate. The listing stays read-only and does not select or start a run.
