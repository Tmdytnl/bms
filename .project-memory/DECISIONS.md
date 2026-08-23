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

## Phase 9 Architecture Core (Frozen)

Freeze record: P9-ARCH-FREEZE-001 after Codex Sol High red-team
(P9-ARCH-SR-001 CHANGE REQUEST accepted and incorporated). These are
long-term architecture decisions. No numeric product-policy values are
recorded here; policy items OP-01 … OP-10 remain OPEN (UNFROZEN).

## D-011 State classification is not safety permission

State (`BMS_State_t`) is operational classification; FAULT is not itself the
FET action. Every potentially active safety source needs an explicit reviewed
CHG/DSG action or an explicit reviewed no-FET-effect.

## D-012 Authoritative source snapshots

Each authoritative safety source (ProtectTask, StateTask, Recovery
Coordinator) exposes its own coherent safety snapshot; the FET manager
consumes these directly.

## D-013 BMS_Data aggregate is diagnostic-only for FET authority

`BMS_Data` aggregated faults are reporting/diagnostics only and must not be
used as the FET manager's real-time safety authority.

## D-014 Directional inhibit action model

Safety outputs carry `inhibit_chg_reasons` / `inhibit_dsg_reasons`; a fault
ID may be active with only one directional inhibit. The FET manager consumes
directional actions and does not infer them from diagnostic fault bits.

## D-015 StateTask execution context + sole scheduler FET writer module

StateTask is the sole scheduler-era context that invokes the FET manager;
the FET manager module is the only scheduler-era writer of SYS_CTRL2 CHG/DSG
state. ProtectTask/Recovery publish safety inputs and may notify StateTask;
they do not write SYS_CTRL2. Pre-scheduler BMS_AfeStartup remains the
startup exception.

## D-016 Protect is the sole runtime XREADY W1C owner

Runtime XREADY W1C is ProtectTask-only; BMS_AfeStartup keeps its separate
pre-scheduler W1C path. Runtime recovery is phaseful and StateTask-serviced;
Protect performs the single W1C only after PRE_CLEAR_READY.

## D-017 Phaseful runtime recovery inhibits both FETs

From first XREADY observation until COMPLETE, recovery-in-progress is a
safety condition with both FETs inhibited. XREADY inactive alone never
releases FET permission; a new XREADY generation aborts and restarts
recovery; failure leaves both inhibited.

## D-018 Calibration provenance

Runtime calibration handoff requires provenance (xready_generation,
recovery_revision, post_clear_verified, calibration); only the Recovery
Coordinator performs it, and Sample accepts it only when generation,
XREADY-inactive, recovery revision, and recovery-state checks pass.

## D-019 Generation-tagged measurements

Measurement publication exposes AFE generation; a valid recovery sample
acknowledgement is sample_sequence + afe_generation; recovery completes only
on the first accepted valid measurement of the current recovery generation.

## D-020 Sequence-tagged State decisions

State/SW safety decisions are tagged with evaluated_sample_sequence and
evaluated_afe_generation and use compare-and-publish; stale publications are
rejected and reevaluated, and FET enable never relies on a decision from an
older measurement generation.

## D-021 Request/ack HW recovery identity

HW_OV/HW_UV/HW_OCD recovery uses request/ack identity tied to request_id,
source_generation, and Protect publication revision; Protect revalidates
currency, performs a fresh SYS_STAT read, rejects on target/blocking status,
clears only its own private active source, and any incompatible event,
generation change, transport failure, or qualification expiry invalidates the
exchange. SYS_STAT low alone is never physical recovery proof.

## D-022 Generation-counter health model

Health uses one naturally aligned uint32_t monotonic generation counter per
required task; each task is sole writer of its own counter; StateTask
snapshots and compares; no heartbeat-bit clear operation exists; counter
inequality across one health window is the progress test.

## D-023 StateTask remains sole IWDG feeder

StateTask is the sole IWDG feeder; IWDG is armed only after the health
baseline is captured and every required task has shown at least one valid
advance; event-driven roster tasks must use bounded waits.

## D-024 Generic latch clear is forbidden

No generic clear-all-latched-fault production API; latch clear must be
source-specific, policy-specific, and evidence-gated.

## D-025 Unmapped active safety source defaults both inhibited

An unknown/unmapped active safety source defaults to both FETs inhibited
until a reviewed action or no-FET-effect exists.

## D-026 Hardware evidence boundary

When BQ I2C is unavailable, software may report requested OFF / enable
inhibited / physical state UNVERIFIED — never physical MOS OFF; SYS_CTRL2
readback proves AFE register-level state only, not physical MOS conduction.

## Phase 8 Artifact Contract v2

## D-027 V1 is historical; v2 is the future gate contract

The Phase 8 NTC/AFE v1 schema and template files remain immutable historical
Git evidence. They are superseded for future gate use by the v2 contracts.

## D-028 BMS canonical JSON v1

`BMS_CANONICAL_JSON_V1` is the frozen artifact canonicalization algorithm.
Strict input rejects duplicate keys, non-integer numeric tokens, negative zero,
out-of-interoperable-range integers, BOM/invalid UTF-8, non-NFC strings, and
unpaired surrogates.

## D-029 Canonical projection hash

Artifact identity is the SHA-256 of the canonical projection that excludes
only `/approval/artifact_sha256`. All other artifact fields remain covered.
This is distinct from raw-file and Git-blob identity.

## D-030 Detached approval authority

Approval fields inside an artifact are declarations, not self-authorization.
External approval requires an independent matching approval record bound to the
artifact revision, schema, canonical projection hash, hardware identity, and
repository identity.

## D-031 Candidate-binding gate manifest

The Phase 8 gate manifest binds exact schemas, artifacts, detached approvals,
generated outputs, evidence, and one production Git candidate. It is neither
approval nor a Hard Gate result.

## D-032 Common hardware identity

NTC and AFE v2 artifacts carry the same structured project, hardware variant,
board, schematic, and exact AFE-part tuple. Pair validation requires exact
tuple equality and compatible BQ datasheet revision evidence.

## D-033 Phase boundary for the AFE artifact

The Phase 8 AFE artifact cannot resolve Phase 9 OPEN product-policy items.
Frozen fail-closed behavior remains in force until separately approved future
policy exists.

## D-034 Source-native units and semantic generated output

AFE delays remain in source-native seconds, milliseconds, or microseconds.
Generated NTC/AFE C output is compared by semantic integer fields, ordering,
and count, never raw struct bytes, padding, or endianness.

## Simulation Development Process

## D-035 Simulation baseline is an authorized development input

`docs/hardware/BMS_V1_模拟硬件参数与产品策略基线.md` is the single
SIM-HW-POLICY-V1 source for current learning-project simulation parameters.

## D-036 Real-hardware unknowns do not block simulation development

REAL_HW_TBD values remain future replacement/validation work. They do not
block the explicitly authorized simulation software closed loop.

## D-037 Hardware evidence remains separate

Simulation, host-test, static-analysis, and target-build evidence are never
hardware validation or production-certification evidence.

## D-038 Separate real-hardware and simulation gates

`firmware/Tests/verify_phase8.py` remains the real-hardware/evidence gate and
must not be weakened to accept simulation inputs as approved hardware.
Simulation implementation uses a separate Phase 9 simulation gate.

## D-039 Policy replacement does not redesign frozen architecture

SIM_POLICY_V1 values are centralized and may later be replaced by verified
hardware/product values without changing frozen Phase 9 ownership contracts.

## D-040 Continuation ownership

Task_SOC is the sole SOC-estimate writer. Task_Balance is the sole
scheduler-era CELLBAL writer, with AFE startup all-zero as the pre-scheduler
exception. CAN remains diagnostic/control-plane support and has no direct FET,
CELLBAL, fault-bitmap, or IWDG authority.

## D-041 Persistence physical writes remain evidence-gated

The A/B record format, CRC32, corruption handling, and wrap-safe newest-slot
selection are implemented. Proposed final-page slots are outside the current
linked image, but physical erase/program scheduling remains deferred until
target timing, power-loss, and endurance safety are demonstrated.

## D-042 Split lower-phase simulator regression

Phase 4, 6, 7, and Phase 8 data/sample/AFE simulator images run in fresh
uVision processes through the split regression runner. This avoids a local
uVision multi-LOAD hang and does not weaken or replace the frozen Phase 8
real-hardware/evidence gate.

## D-043 Simulation resource baseline

The simulation-integrated FreeRTOS heap is 12 KiB. ARMCC5 callgraph evidence
sets explicit task stack allocations, and the production link reports 15,632
bytes of static RW+ZI RAM. These are target-build facts, not physical runtime
high-water validation.
