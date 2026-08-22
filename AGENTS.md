<!-- project-continuity:start -->
## Project continuity

For normal project tasks, restore current project context from these three files:

1. `.project-memory/PROJECT_STATUS.md`
2. `.project-memory/TASK_BOARD.md`
3. `.project-memory/DECISIONS.md`

After reading them, inspect current Git evidence before substantive work:

- `git status --short --branch`
- current branch / HEAD
- recent Git history relevant to the task

Git, current source, tests, build evidence, and current user instructions are the source of truth.
If they disagree with project-memory files, current repository evidence and current user instructions win.

The following legacy memory system is deprecated and read-only during normal work:

- `.project-memory/PROJECT_LOG.md`
- `.project-memory/archive/`
- `.project-memory/pending/`
- `.agents/skills/project-log-memory/`

Do not invoke `project-log-memory` or run `project_log.py` during normal development, review, documentation, or handoff tasks.

Only inspect legacy memory when a task explicitly requires historical audit or legacy-memory maintenance.

Do not make project-memory maintenance a blocker for firmware, review, build, test, or documentation work.
If a memory update encounters an unexpected conflict, stop the memory update and report it; do not expand the current task to repair legacy infrastructure.

Only update the three active V2 memory files when the current task explicitly authorizes those changes.
<!-- project-continuity:end -->

## Repository layout

- Treat `docs/` as read-only project input/reference material. Do not place generated reports, summaries, review outputs, or firmware source code under `docs/`.
- Place generated human-facing documents under `deliverables/`, grouped by purpose (for example `deliverables/review/`).
- Place future firmware source, project files, linker descriptions, and tests under `firmware/`. Keep generated code separate from both `docs/` and `deliverables/`.
