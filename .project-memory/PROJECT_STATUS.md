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

## Open Documentation Finding

L-01 remains open:

Phase8 Report contains historical/stale Git-state wording and states Sample priority as 3, while production app_rtos.h defines Sample priority as 4.

- docs-only
- non-blocking for this infrastructure migration
- not fixed by BMS-INFRA-001

## Current Next Actions

1. Fix Phase8 Report L-01 as separate docs-only task.
2. Obtain approved immutable NTC artifact.
3. Obtain approved immutable AFE startup/protection policy artifact.
4. Re-run future Phase8 gate only after approved artifacts exist.
5. Later Codex Sol High safety review.

## Invariants

- Git/current repository evidence outranks memory.
- No guessed production calibration/protection parameters.
- Simulator evidence is not hardware validation.
- Hardware validation remains separate.
- Phase9 implementation is not officially started while Phase8 Hard Gate remains BLOCKED under current process rule.
- Firmware/test/verifier changes require an explicit task.
