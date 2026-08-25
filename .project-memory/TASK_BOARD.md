# BMS V1 Task Board

## Active Task

None.

## Open Queue

### P8-BLOCKER-NTC

Goal:
Obtain approved immutable NTC production artifact.

Status:
REAL-HARDWARE VALIDATION / REPLACEMENT TODO

### P8-BLOCKER-AFE

Goal:
Obtain approved immutable AFE startup/protection policy artifact.

Status:
REAL-HARDWARE VALIDATION / REPLACEMENT TODO

### P8-SAFETY-REVIEW

Goal:
Later Codex Sol High Phase8 safety review.

Status:
WAITING

## Completed Tasks

### BMS-ENGINEERING-CLOSURE-M3

Goal:
Close the simulation release candidate into a source-grounded engineering,
bring-up, validation, learning and release baseline without changing frozen
safety architecture or claiming real-hardware evidence.

Status:
COMPLETED — PASS SOFTWARE / SIMULATION RELEASE BASELINE

Result:
- repository/module/source-quality completeness sweep completed
- architecture, task ownership and runtime walkthrough documented
- staged hardware bring-up guide plus hardware/software validation matrices added
- debugging, configuration, engineering-story and interview guides added
- root README and current test entrypoints established
- low-risk USART1 read-only observability added; no command/control path
- Flash/RAM/stack/queue resource review completed with exact ARMCC5 evidence
- 32 scenarios, 3 races, 50,000 stress iterations, lower regressions and
  49 trust-chain tests passed
- ARMCC5 final Clean/Rebuild passed 0 errors / 0 warnings
- Software Release Baseline established
- REAL_HW validation remains pending; no independent-review claim made

### BMS-SIM-CLOSED-LOOP-M1

Goal:
Complete the BMS V1 simulation software closed loop from Phase 8 integration
through Phase 9, then continue into SOC, balancing, CAN, safe Flash scope, and
full simulator integration.

Status:
COMPLETED — PASS FOR SIMULATION

Result:
- P8-V2-SR-N01 closed and SIM-HW-POLICY-V1 landed
- centralized SIM_POLICY_V1 and Phase 8 simulation integration passed
- Phase 9 safety/state/FET/recovery/health/IWDG closed loop passed
- SOC and balancing passed
- CAN protocol/core and target bxCAN binding passed in software; physical bus deferred
- persistence A/B codec and target erase/program scheduling passed in software;
  physical brownout/endurance/timing deferred
- 32 simulator scenarios and 3 targeted races completed with zero failures
- ARMCC5 production rebuild passed with 0 errors and 0 warnings
- no hardware-validation or production-certification claim

### P8-BLOCKER-V2-001

Goal:
Implement Phase 8 artifact contract v2 and preflight trust tooling.

Status:
COMPLETED

Result:
- v2 NTC/AFE approved-artifact schemas and intentionally invalid templates
- BMS_CANONICAL_JSON_V1 strict parser and canonical projection hash
- detached approval record and candidate-binding gate manifest
- offline validator, golden vectors, and synthetic regression tests
- v1 package superseded for future gate use but preserved as historical evidence
- Blocker-1 and Blocker-2 unchanged; Phase8 Hard Gate remains BLOCKED(2)

### P8-BLOCKER-PACK-SR-001

Goal:
Independent safety review of the Phase 8 blocker intake package v1.

Status:
COMPLETED — BLOCK

Result:
- 3 Critical, 6 High, 2 Medium findings
- v1 artifacts retained as historical Git evidence
- v1 contract prohibited from becoming the future gate contract

### P8-BLOCKER-PACK-001

Goal:
Create machine-fillable Phase 8 blocker intake package (templates + schemas + approval checklist + future gate binding plan).

Scope:
docs/schema only

Status:
COMPLETED

Result:
- intake package prepared (deliverables/phase8/input_templates/ + approval checklist + gate binding plan)
- SUPERSEDED FOR FUTURE GATE USE by P8-BLOCKER-V2-001
- no approved values supplied
- blockers unchanged

Completion commit:
`docs: add phase8 blocker intake package` (hash in git history)

### P9-ARCH-FREEZE-001

Goal:
Freeze Phase 9 Architecture Core v1 after Codex Sol High red-team.

Scope:
docs-only (architecture document + Project Memory V2)

Status:
COMPLETED

Result:
- Phase 9 Architecture Core v1 FROZEN (contracts FROZEN-01 … FROZEN-18 + snapshot model)
- P9-ARCH-SR-001 change request accepted and incorporated
- OPEN product-policy items OP-01 … OP-10 remain unfrozen
- Phase8 Hard Gate remains BLOCKED(2); Phase9 official implementation remains NOT STARTED
- no firmware/test/verifier changes

Completion commit:
`docs: freeze phase9 safety architecture core` (hash in git history)

### P9-ARCH-SR-001

Goal:
Codex Sol High red-team of the Phase 9 architecture draft.

Status:
COMPLETED — CHANGE REQUEST

Result:
- change request accepted and incorporated into the frozen core (P9-ARCH-FREEZE-001)

### P9-ARCH-BATCH-01

Goal:
Produce the Phase 9 architecture preparation draft.

Status:
DRAFT COMPLETED

Result:
- evidence-backed architecture draft produced; superseded by P9-ARCH-FREEZE-001 (frozen core)

### P8-DOC-002

Goal:
Correct Phase8 Report L-01 only.

Scope:
docs-only

Status:
COMPLETED

Completion commit:
f2b32e32e2e34c0819af7e330d731c32bf9ffc3c

Result:
- stale/historical Git-state wording clarified
- Task_Sample priority corrected from 3 to 4
- no firmware/test/verifier changes
- Phase8 gate not rerun because task was docs-only

## Blocked Work

Real-hardware Phase 8 qualification remains blocked on approved immutable NTC
and AFE artifacts. This does not block the active simulation milestone.

## Completed Infrastructure

BMS-INFRA-001
Project Memory V2 bootstrap

Completed when:
the four-file scoped commit is created and validation passes.
