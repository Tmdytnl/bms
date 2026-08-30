# BMS V1 Simulation Closed-Loop Integration Report

> `BMS-SIM-CLOSED-LOOP-M1` milestone evidence. The final project conclusion,
> frozen architecture and complete regression set are recorded in
> `deliverables/release/BMS_V1_Release_Baseline.md`.

Task: `BMS-SIM-CLOSED-LOOP-M1`

Profile: `SIM-HW-POLICY-V1` / `SIM_POLICY_V1`

Result: **MILESTONE PASS**

Evidence set: production-C Simulator scenarios, static checks, targeted races and ARMCC5 target build.

## 1. Implemented closed loop

- Phase 8: the simulation hardware/policy baseline is wired through the centralized immutable policy module; AFE startup, NTC conversion, current mapping, SampleTask, and measurement provenance are integrated.
- Phase 9: authoritative Protect and State safety snapshots, directional software protection, state classification, single-writer FET manager, phaseful XREADY recovery, generation-bound calibration handoff, source-specific HW recovery handshake, task health, and StateTask-owned IWDG decisions are integrated.
- SOC: integer OCV initialization and coulomb counting, charge/discharge efficiency, saturation, full/empty correction, generation binding, and explicit queue-gap invalidation are integrated.
- Balancing: eligibility/inhibit checks, hysteresis, two-cell maximum, adjacent-cell exclusion, rotation, sole scheduler-era CELLBAL ownership, register readback, and fail-safe all-off behavior are integrated.
- CAN: six explicit 11-bit diagnostic frames (`0x180` through `0x185`), bounded Rx handling, service-reset request decoding at `0x280`, source-specific routing and non-safety diagnostics are integrated；current Release Baseline also includes bxCAN BSP binding and static timing/filter/ISR verification.
- Persistence: the milestone A/B record codec、CRC32、corruption handling and wrap-safe newest-record selection are retained；current Release Baseline extends this to 34-byte v2 records、page-restricted target adapter、commit-last、readback verification and power-cut regression.

## 2. Scenario matrix

| Scenarios | Coverage | Result |
|---|---|---|
| SIM-01..04 | startup, standby, charge, discharge | PASS |
| SIM-05..12 | SW/HW OV, SW/HW UV, charge/discharge OC, OCD, SCD | PASS |
| SIM-13..16 | charge/discharge cold and hot | PASS |
| SIM-17..19 | data stale, burst/continuous AFE communication failures | PASS |
| SIM-20..21 | XREADY recovery and new XREADY during recovery | PASS |
| SIM-22 | ambiguous FET enable | PASS |
| SIM-23..24 | SampleTask and ProtectTask stalls | PASS |
| SIM-25 | balancing selection and transport-failure safe-off | PASS |
| SIM-26..28 | SOC charge, discharge, and full correction | PASS |
| SIM-29 | persistence A/B codec, CRC, corruption, and sequence wrap | PASS (codec only) |
| SIM-30 | CAN absent while core remains functional | PASS |
| SIM-31..32 | simultaneous faults and timer wraparound | PASS |

Scenario total: **30 completed, 0 failed**.

Targeted race total: **3 completed, 0 failed** (Protect revision during enable, XREADY generation during calibration handoff, and new HW event during recovery qualification). The stale-measurement-before-State-publish race is also exercised by compare-and-publish logic tests.

## 3. Test and build results

- Phase 9/continuation ARMCC5 simulator image: completed; overall failures `0`.
- Phase 9 categories: logic `0`, FET `0`, recovery `0`, health `0`, HW handshake `0` failures.
- Continuation categories: SOC `0`, Balance `0`, CAN `0`, persistence codec `0` failures.
- Split lower-phase ARMCC5 simulator regression: Phase 4, 6, 7, Phase 8 data/sample/AFE all completed with `0` failures; Phase 7 simulated communication and Phase 8 sample provenance guards passed.
- Production Keil/ARMCC5 5.06u7 rebuild: **0 errors, 0 warnings**.
- Production image size: Code `44,724` bytes, RO data `1,056`, RW data `340`, ZI data `15,292`; static RW+ZI RAM `15,632` bytes.
- Phase 9 simulation-development verifier: **PHASE9 SIMULATION GATE PASS**.
- Phase 8 artifact/manifest trust-chain suite: **49 tests passed**, including the P8-V2-SR-N01 adversarial cases.
- Phase 8 trust-chain validator remains unchanged and binds schema、artifact identity、approval record、manifest and source tables.

## 4. Safety ownership checks

- State classification is not used as generic FET safety permission.
- Protect remains sole runtime XREADY W1C owner.
- FET Manager remains sole scheduler-era SYS_CTRL2 CHG/DSG writer and performs full-byte readback/revalidation.
- Balance remains sole scheduler-era CELLBAL writer; startup all-off is the pre-scheduler exception.
- `BMS_Data` remains diagnostic-only for FET authority.
- Runtime calibration evidence is bound to XREADY generation and recovery revision.
- StateTask remains sole IWDG feeder.
- No generic clear-all-latched-fault API was introduced; CAN service reset is a request routed through source-specific evaluation.

## 5. Final Release Baseline closure

| Milestone area | Final closure evidence |
|---|---|
| NTC / AFE / product profile | Central immutable `SIM-HW-POLICY-V1`、structural validation、source-table verifier and 49 trust-chain tests |
| FET control | Scheduler-era single writer、directional inhibits、write/readback/revision confirmation、ambiguity quarantine and targeted race tests |
| IWDG | BSP target binding、generation-based health model、StateTask sole feeder and stall scenarios |
| CAN | Explicit protocol、bxCAN target binding、filter/timing/ISR verifier and continuation scenarios |
| Flash persistence | Page-restricted target adapter、A/B v2 record、commit-last、power-cut tests and map boundary |
| Project closure | 32 deterministic scenarios、3 targeted races、50,000 stress iterations、ARMCC5 0/0 and identical HEX |

BMS V1 的最终状态以 Release Baseline 中的 `Engineering Closure: COMPLETE`、`Release Baseline: ESTABLISHED` 与 `Open Blockers: NONE` 为准。
