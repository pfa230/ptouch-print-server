---
name: loop-propose
description: Propose an OpenSpec change through the project's native proposing workflow, then summarize the proposed design and changes. Use when the user asks to propose a change for the loop or invokes loop-propose.
---

# Propose a change with a design summary

**Explain decision questions.** Apply this to every decision question asked while following native proposing, including initial clarification and questions raised while creating artifacts. Before any compact question picker, write the relevant context, what the answer changes, the options' practical tradeoffs, and the recommended option with its reason, grounded in the user's request and project context, as self-contained user-facing prose, so the user can answer without opening artifacts or tool output. Keep the explanation proportional to the decision and keep short option labels for selection.

1. **Delegate to native proposing.** Load the project's installed native OpenSpec proposing skill or command for this harness, such as the `openspec-propose` skill or `/opsx:propose` command in Claude Code and the `openspec-propose` skill in Codex, and follow it with the user's request. Keep the same working directory and any store the user or project selects. Native proposing owns root and schema selection, artifact creation, clarification and its planning boundary. If no native proposing skill or command is installed, report that missing prerequisite and stop; do not plan without it.
2. **Summarize before the inventory.** When native proposing has completed the artifacts, re-read them from disk. Before native proposing's artifact list and readiness handoff, give a concise account, grounded in those artifacts, of what the change does to the project and how the proposed design works. Then present native proposing's artifact list and readiness handoff.
3. **Stop at the planning boundary.** Do not implement the change or start a loop run. When `loop-scope` invoked this skill, return to its review and commit workflow instead of waiting for a new user request.
