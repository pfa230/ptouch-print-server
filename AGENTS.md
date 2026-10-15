# Agent instructions

These rules apply to every agent working in this repository.

- **Minimal by design.** Every mechanism, option and check must serve a concrete need in `openspec/specs/` or in the change being made. Do not add speculative configuration, plugin points, extra layers, silent fallbacks, or gates for work a person does by hand. When two designs both work, use the one with fewer moving parts.
- **Cut before presenting.** After the last artifact or edit and before any summary, run a cut pass, even when a skill's own steps end earlier. List each mechanism, option, check, state field, record and task on its own line, and name the specification clause, as the change amends it, or the user's explicit request it serves where the specifications are silent, such as docs or tooling. Remove whatever names neither, and whatever duplicates what the repository already has. For each item that stays, name its simpler version and the concrete failure that version causes; when there is none, use the simpler version. The presentation includes the cut list: what was removed, and why each kept item stayed.
