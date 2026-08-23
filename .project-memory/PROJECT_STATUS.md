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
- Phase 8 simulation profile: SIM-HW-POLICY-V1 ACTIVE
- Phase 8 simulation development readiness: PASS
- Phase 8 simulation integration: PASS
- Phase 8 real-hardware qualification: DEFERRED / REAL_HW_TBD
- Phase 9 Architecture Core v1: FROZEN
- Phase 9 simulation implementation: PASS FOR SIMULATION
- SOC simulation module: PASS
- Balance simulation module: PASS
- CAN protocol/core simulation module: PASS; target peripheral binding DEFERRED
- Persistence A/B format/codec: PASS; physical Flash erase/program DEFERRED
- Full simulation integration: PASS (30 scenarios + 3 targeted races, zero failures)
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

These remain real-hardware qualification blockers. They do not block the
explicit SIM-HW-POLICY-V1 learning/simulation development path authorized by
BMS-SIM-CLOSED-LOOP-M1. `verify_phase8.py` remains the real-hardware/evidence
gate; simulation development uses a separate Phase 9 gate.

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

1. Keep SIM_POLICY_V1 values centralized and preserve frozen Phase 9 ownership.
2. Add and validate the target CAN BSP when board/transceiver evidence is available.
3. Add physical Flash erase/program scheduling only after target timing, power-loss,
   and endurance safety are demonstrated.
4. Obtain approved immutable NTC and AFE artifacts for real-hardware qualification.
5. Re-run the real-hardware Phase 8 gate only after approved artifacts exist.
6. Perform physical-hardware validation separately after real hardware is available.

## Invariants

- Git/current repository evidence outranks memory.
- No guessed production calibration/protection parameters.
- Simulator evidence is not hardware validation.
- Hardware validation remains separate.
- SIM_POLICY_V1 is a development input, not a production or hardware claim.
- The Phase 8 real-hardware gate remains blocked while simulation implementation may proceed under the explicit learning-project process rule.
- Firmware/test/verifier changes require an explicit task.
