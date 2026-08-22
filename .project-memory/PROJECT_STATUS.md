# BMS V1 Project Status

## Purpose

- This file is a current-status navigation aid, not a history log.
- Git / source / tests / build evidence are the source of truth.
- Current HEAD is not copied into this file.
- Read HEAD live from Git when needed.

## Current Phase

- Phase 1-3: VALIDATED
- Phase 4-7: reviewed/repaired
- Phase 8 software implementation: PASS
- Phase 8 regression/build evidence: PASS
- Phase 8 Hard Gate: BLOCKED (2)
- Phase 9: NOT STARTED
- Hardware validation: DEFERRED / SEPARATE

## Active Development Branch

`codex/phase8-phase9`

Current HEAD must be obtained from Git at task start.

## Stable Phase 8 Firmware / Evidence Baseline

`c9f1813096dcea23d7e9495a00c07e6414595c44`

This is the Phase8 firmware/evidence baseline, not the dynamic repository HEAD.

## Phase 8 Review State

WorkBuddy independent review:

ACCEPT FOR SAFETY REVIEW

This does not lift the Hard Gate blockers.

## Hard Gate Blockers

- BLOCKER-1: approved immutable production NTC curve/table artifact missing.
- BLOCKER-2: approved immutable AFE startup/protection policy artifact missing, including complete startup policy and OV / UV / OCD / SCD / Rsense / runtime calibration handoff / XREADY recovery / FET enable policy.

No guessed parameters are recorded.

## Documentation Review Status

L-01: CLOSED

Closed by:
P8-DOC-002

Completion commit:
f2b32e32e2e34c0819af7e330d731c32bf9ffc3c

Summary:
- historical Git-state wording clarified
- Task_Sample priority corrected to production value 4

Note: the completion commit is task-closure evidence, not the dynamic repository HEAD.

## Current Next Actions

1. Obtain approved immutable NTC artifact.
2. Obtain approved immutable AFE startup/protection policy artifact.
3. Re-run future Phase8 gate only after approved artifacts exist.
4. Perform later Codex Sol High Phase8 safety review as required by the project review flow.
5. Phase9 implementation remains NOT STARTED until the Hard Gate/process rule permits it.

## Invariants

- Git/current repository evidence outranks memory.
- No guessed production calibration/protection parameters.
- Simulator evidence is not hardware validation.
- Hardware validation remains separate.
- Phase9 implementation is not officially started while Phase8 Hard Gate remains BLOCKED under current process rule.
- Firmware/test/verifier changes require an explicit task.
