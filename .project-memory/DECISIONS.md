# BMS V1 Durable Decisions

## D-001 Git is the source of truth

Git/source/tests/build evidence/current user instruction outrank project-memory summaries.

## D-002 Project Memory V2 has three active files

Only:

- PROJECT_STATUS.md
- TASK_BOARD.md
- DECISIONS.md

are normal handoff inputs.

## D-003 Legacy memory is frozen

Legacy:

- PROJECT_LOG.md
- archive/
- pending/
- project-log-memory skill

remain available for forensic/history use, but are not part of normal task startup or closeout.

## D-004 No automatic helper workflow

Normal tasks must not automatically run:

- project_log.py
- candidate generation
- archive merge
- compression
- strict-limit repair

Memory maintenance must never silently expand another task.

## D-005 Scope control

Agent tasks must obey explicit:

- goal
- allowed files
- forbidden files
- validation
- stop conditions

Agents must not expand scope simply because they discover adjacent issues.

## D-006 Repository layout

Preserve existing policy:

- docs/: read-only project input/reference
- deliverables/: generated reports/reviews
- firmware/: production source/project/tests

## D-007 Safety / parameter policy

No guessed:

- NTC curve
- protection thresholds
- Rsense production facts
- calibration policy

Production policy inputs require approved immutable artifacts.

## D-008 Phase gate rule

Phase8 Hard Gate currently remains BLOCKED(2).

Phase9 implementation remains officially NOT STARTED unless the gate passes or the user explicitly changes the process rule.

## D-009 Hardware evidence boundary

Simulator/build/static review evidence must not be represented as hardware validation.

## D-010 Critical ownership invariants

Preserve already established architecture:

- CHG/DSG control follows the established single-writer safety ownership.
- StateTask remains the health supervisor / sole IWDG feeder unless changed by an explicit reviewed task.
- ALERT/XREADY/CC safety semantics must not be casually changed by infrastructure tasks.
