# Agent instructions

These rules apply to every agent working in this repository.

- **Minimal by design.** A change contains only what a concrete delivery need requires. This holds for each part of the plan on its own:
  - Each sentence and scenario a spec delta adds or rewrites states behavior that the existing specs in `openspec/specs/` do not already imply. A delta line that restates an existing clause, or a scenario for a case the delta's own condition already excludes, breaks this rule.
  - Each mechanism, option, check, state field, record, test and task serves a clause of the existing specs, a delta line that meets the point above, or, where the specs are silent, a request the proposal records, such as docs or tooling. None duplicates what the repository already has.
  - Each item is the smallest version that serves its clause or request. A larger version needs a failure of the smaller one, shown by a run, a test or the code as it stands. A test, recording or check that guards only against a risk no run has shown breaks this rule.
  - It adds no speculative configuration, plugin points, extra layers, silent fallbacks, or gates for work a person does by hand.
- **Cut before presenting.** After the last artifact or edit and before any summary, run a cut pass against "Minimal by design", even when a skill's own steps end earlier. List each spec sentence and scenario the change adds or rewrites, then each mechanism, option, check, state field, record, test and task, each on its own line with what it serves and the failure its smaller version causes. Remove what fails the rule. The presentation includes the cut list: what the draft contained and the pass removed, and why each kept item stayed.
- **Deliver through the loop.** A change is implemented by running its committed proposal with `loop run <change>`, supervised through the `openspec-loop` skill, not by applying it by hand. A proposal ends with its committed artifacts: start the loop, or apply the change by hand, only when the user asks for that.
