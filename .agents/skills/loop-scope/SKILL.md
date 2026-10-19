---
name: loop-scope
description: Prepare selected GitHub issues as individually committed OpenSpec proposals in an attended scoping worktree, then deliver them together. Use when the user asks to scope issues with the loop or invokes loop-scope.
---

# Attend a scoping session

Prepare native OpenSpec proposals with the user. The deterministic commands create and deliver the session; the user chooses issues, settles material decisions and chooses when to finish. Scoping does not implement, enroll or archive changes, and leaves source issues open.

## Permissions and working directories

- Read applicable repository instructions first, including their issue/Git workflow. Draft proposals with the `loop-propose` skill, which delegates to the repository's native OpenSpec proposing, rather than a replacement planner.
- Run `loop scope start <session>` and `loop scope finish <session>` outside a sandbox that makes the shared Git directory read-only. Proposal staging and commits need the same Git-write permissions. In Codex, request escalated permissions for these commands, as the supervision skill does. A permission refusal does not justify authoring in the default checkout.
- Remember the checkout where start was invoked. Both commands use the existing configuration merge order: invoking `openspec/loop.yaml`, shared main-checkout `openspec/loop.local.yaml`, then the invoking checkout's local overrides; finish from that same checkout unless the user deliberately chooses different settings. Shared main-checkout overrides are inherited when configuration is read; invoking overrides are not copied into the session.
- Set the reported session worktree explicitly as the working directory for `loop-propose`, all OpenSpec context/artifact commands and proposal commits. Run `openspec list --json` there and inspect its resolved root, then inspect native instructions' artifact paths before writing or staging. Require those paths to resolve inside the session worktree. If the project selects a store, follow native store selection; if its resolved root is outside the session worktree, stop and explain the conflict rather than write outside it or silently switch roots.

## Select and start

1. List open issues using the repository's issue-listing workflow. Otherwise read them with `gh issue list` and relevant details with `gh issue view`; explain an order favoring blockers, correctness work and small work whose decisions are settled. Wait for the user to select issues before drafting. An issue selection already given by the user counts; do not ask for it again.
2. Use the user's session name if supplied; otherwise derive a short descriptive name from the selected issues without asking for confirmation. Run `loop scope start <session>` from the invoking checkout. It creates `scope/<session>` and `<worktrees>/scope-<session>` from the configured local default branch without fetching or changing that branch. Git validates the generated branch name. Show the reported path, branch and finish command.
3. To continue interrupted work or handle an existing-session refusal, inspect `git worktree list`, then read the existing session's files, `git status` and `git log`. Reopen that worktree without replacing the branch or checkout. If finish cannot find a session during a detached manual rebase, complete or abort that rebase or restore its checkout before retrying. Completed commits and unfinished drafts are the recovery record; no separate session state is needed.

## Propose one selected issue at a time

1. Read the selected issue, its comments and relevant project context. Invoke `loop-propose` from the session worktree for one coherent implementable change. Verify its root and artifact paths as above.
2. `loop-propose` returns to this scoping workflow for artifact review and commit. Show its summary and the artifacts and continue without requiring a new user request unless the user requested a review pause. Do not start implementation.
3. Inspect the generated proposal, design, spec deltas and tasks. Ask about unresolved choices that materially affect behavior or scope and stop until answered. Use explicit defaults for routine choices, recording them as defaults in `design.md`; record the user's material decisions there as well. Do not silently choose behavior so later unattended implementation can proceed.
4. Trim to one coherent change and record non-goals. Explicit invocation of this skill authorizes filing omitted requested work as follow-up GitHub issues. Follow the repository's issue conventions, check for an existing follow-up, and create only the necessary follow-ups. Each must link the source issue URL and name the OpenSpec change; link it from the proposal's non-goals. The proposal need not be published and no proposal URL is required. Use a structured body argument or a body file for multiline issue text. Do not file unrelated work or close the source issue.
5. Record `Source issue: <URL>` in `proposal.md`. Once that proposal's artifacts are ready, inspect its diff and stage only `openspec/changes/<change>/` in the session worktree. Inspect the staged paths to avoid including another proposal or unrelated staged work, then commit that proposal separately, for example:

   ```sh
   git add -- openspec/changes/<change>/
   git diff --cached --name-only
   git commit -m 'docs(scope): propose <change> (relates to #14)' -- openspec/changes/<change>/
   ```

   Use the actual change name and source issue number, without closing keywords. Each completed proposal gets its own artifact-only commit before moving to another issue. Do not invent artifact-content checks or Git hooks.
6. Show the finished proposal commit and remaining issues, then proceed to the next selected issue unless the user requested a review pause. When the selection is exhausted, report the completed proposals and wait for the user to choose more issues or finish. If interrupted, leave the draft in place; resume by inspecting the same files and Git history.

## Finish and recover

When the user chooses to finish, run `loop scope finish <session>` from the original invoking checkout. Relay its progress and outcome, including external effects and the reported recovery. Exit 0 means delivery succeeded, 1 means integration/publication failed, and 2 means a refusal. Keep the session branch and worktree.

Finish requires no uncommitted tracked or untracked session work. An unfinished draft refuses with exit 2 and remains available. Continue authoring it or let the user deliberately move it elsewhere; do not commit, stash or delete drafts just to finish.

- `local` rebases onto the local default branch and fast-forwards it without contacting a remote. This differs from local run delivery, which leaves the default branch untouched.
- `pr` fetches and rebases onto the default branch's upstream, pushes the session branch without force, then opens or finds one PR targeting that upstream branch/repository. The local default branch stays unchanged. `gh pr create --fill` carries the issue-bearing commit subjects into the PR.
- `merge` fetches and synchronizes the local default branch safely, rebases onto it, pushes to its upstream default branch, then fast-forwards the local default branch. Dirty default holders, ahead/diverged defaults and obstructing files require attended recovery.

A conflicting finish rebase is aborted. Explain Git's error and reconcile in the session worktree before retrying; no automatic merge fallback or force push is allowed. Preserve any pre-existing user operation. Do not retry until the user chooses to continue after resolving the cause.

If a merge push succeeded before the local update failed, retry can complete that safe update. An open PR is reused only after a successful non-force push. When a moved base causes an already-published PR branch to be rebased and its push rejected, reconcile both histories in the session worktree:

1. Fetch the configured upstream remote's published session branch and current base.
2. Merge the published session history into the rebased local session branch.
3. Merge the current base if it is not already an ancestor of the session tip.
4. Resolve and commit any conflicts, then retry finish from the original invoking checkout when the user chooses.

For example, with upstream `origin/main` and session `planning`:

```sh
git fetch origin scope/planning main
git merge --no-edit origin/scope/planning
git merge --no-edit origin/main       # if not already an ancestor of HEAD
```

Both the published tip and current base must be ancestors of the session tip, so finish skips another rebase and can push without force. Resetting only to the published tip repeats the rebase and rejection. Use the actual configured remote/branch names and retain Git's errors for attended recovery.

After delivery, tell the user to enroll a proposal explicitly with `loop run <change>` or `loop step <change>` from the default branch once it has landed there. In `pr` mode the proposal PR must be merged first. Enrollment from the multi-proposal session branch includes sibling proposal commits in the selected change's patch. Do not enroll automatically. Source issues remain open until their changes are implemented; session cleanup remains ordinary manual Git work.
