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
- Phase 9 Architecture Core v1: FROZEN
- Phase 9 official implementation: NOT STARTED
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

## Phase 9 Architecture State

- Phase 9 Architecture Core v1: **FROZEN** (contracts FROZEN-01 … FROZEN-18).
- Codex architecture red-team (P9-ARCH-SR-001): **CHANGE REQUEST accepted and incorporated**.
- The freeze covers architecture contracts only.
- Product-policy items listed as OPEN remain **UNFROZEN**.
- Architecture freeze does NOT start Phase 9 implementation and does NOT lift the Phase 8 Hard Gate.

## Hard Gate Blockers

- BLOCKER-1: approved immutable production NTC curve/table artifact missing, including the NTC conversion domain.
- BLOCKER-2: approved immutable AFE startup/protection policy artifact missing, including Rsense/current mapping and polarity, AFE HW OV/UV/OCD/SCD targets/delays, runtime calibration handoff policy, complete AFE startup policy, XREADY recovery policy, and FET enable/startup policy.

No guessed parameters are recorded.

## Phase 8 Blocker Intake Package

P8-BLOCKER-PACK-SR-001: **BLOCK** (3 Critical, 6 High, 2 Medium).

The historical v1 intake package remains immutable Git evidence but is
**SUPERSEDED FOR FUTURE GATE USE**.

Phase8 artifact contract v2 and offline preflight tooling:
**READY** (P8-BLOCKER-V2-001).

Meaning: approved-artifact v2 schemas/templates, strict
`BMS_CANONICAL_JSON_V1`, detached approval record, candidate gate manifest,
offline validator, golden vectors, and synthetic regression tests exist under
`deliverables/phase8/` and `tools/phase8/`.

This does NOT resolve Blocker-1 or Blocker-2.

## Phase 9 Open Product-Policy Items (UNFROZEN)

- OP-01 XREADY historical latch reset authority/evidence
- OP-02 SCD reset authority/evidence
- OP-03 OVRD_ALERT reset authority/evidence
- OP-04 continuous AFE_COMM definition + latch/reset policy
- OP-05 OCD escalation / latch policy
- OP-06 SW OV/UV/OC production thresholds/hysteresis/debounce
- OP-07 temperature direction/cutoff/hysteresis/delay
- OP-08 DATA_STALE directional FET action
- OP-09 required-task health roster / exact liveness windows
- OP-10 IWDG hardware timeout / arming timing values

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

1. Obtain approved immutable NTC artifact (Blocker-1).
2. Obtain approved immutable AFE startup/protection policy artifact (Blocker-2).
3. Re-run future Phase8 gate only after approved artifacts exist.
4. Perform later Codex Sol High Phase8 safety review as required by the project review flow.
5. Resolve Phase9 OPEN product-policy items OP-01 … OP-10 via approved policy artifacts; do not guess values.
6. Fill and independently approve the v2 Phase8 blocker artifacts; detached approval and preflight do not themselves pass the gate.
7. Phase9 official implementation remains NOT STARTED until the Hard Gate/process rule permits it.

## Invariants

- Git/current repository evidence outranks memory.
- No guessed production calibration/protection parameters.
- Simulator evidence is not hardware validation.
- Hardware validation remains separate.
- Phase9 official implementation is not started while Phase8 Hard Gate remains BLOCKED under current process rule; the Phase9 architecture freeze does not change this.
- Firmware/test/verifier changes require an explicit task.
