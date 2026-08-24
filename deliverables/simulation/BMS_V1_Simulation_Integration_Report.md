# BMS V1 Simulation Closed-Loop Integration Report

> Historical milestone record. Superseded by
> `BMS_V1_Simulation_RC_Report.md` for the current Simulation Release
> Candidate; counts and deferred-binding statements below describe the
> earlier `BMS-SIM-CLOSED-LOOP-M1` revision.

Task: `BMS-SIM-CLOSED-LOOP-M1`

Profile: `SIM-HW-POLICY-V1` / `SIM_POLICY_V1`

Result: **PASS FOR SIMULATION**

Evidence boundary: software simulation, static checks, and ARMCC5 target build only; no physical-hardware validation is claimed.

## 1. Implemented closed loop

- Phase 8: the simulation hardware/policy baseline is wired through the centralized immutable policy module; AFE startup, NTC conversion, current mapping, SampleTask, and measurement provenance are integrated.
- Phase 9: authoritative Protect and State safety snapshots, directional software protection, state classification, single-writer FET manager, phaseful XREADY recovery, generation-bound calibration handoff, source-specific HW recovery handshake, task health, and StateTask-owned IWDG decisions are integrated.
- SOC: integer OCV initialization and coulomb counting, charge/discharge efficiency, saturation, full/empty correction, generation binding, and explicit queue-gap invalidation are integrated.
- Balancing: eligibility/inhibit checks, hysteresis, two-cell maximum, adjacent-cell exclusion, rotation, sole scheduler-era CELLBAL ownership, register readback, and fail-safe all-off behavior are integrated.
- CAN: six explicit 11-bit diagnostic frames (`0x180` through `0x185`), bounded Rx handling, service-reset request decoding at `0x280`, source-specific routing, and non-safety CAN diagnostics are integrated. A target CAN peripheral/BSP is not present, so physical bus binding is deferred.
- Persistence: a fixed 32-byte A/B record codec, CRC32, corruption handling, and wrap-safe newest-record selection are integrated. The linker map proves the proposed slots (`0x0800F800`, `0x0800FC00`) are outside the current image, but safe runtime erase/program scheduling is not demonstrated; physical Flash writes remain deferred.

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
- The frozen Phase 8 real-hardware verifier remains unchanged and is not used to convert simulation evidence into an approved hardware result.

## 4. Safety ownership checks

- State classification is not used as generic FET safety permission.
- Protect remains sole runtime XREADY W1C owner.
- FET Manager remains sole scheduler-era SYS_CTRL2 CHG/DSG writer and performs full-byte readback/revalidation.
- Balance remains sole scheduler-era CELLBAL writer; startup all-off is the pre-scheduler exception.
- `BMS_Data` remains diagnostic-only for FET authority.
- Runtime calibration evidence is bound to XREADY generation and recovery revision.
- StateTask remains sole IWDG feeder.
- No generic clear-all-latched-fault API was introduced; CAN service reset is a request routed through source-specific evaluation.

## 5. Deferred real-hardware work and limitations

| Item | Reason | Blocks simulation software? | Future evidence needed |
|---|---|---:|---|
| Real NTC/AFE/product parameter qualification | Approved immutable production artifacts and physical measurements are not available | No | Approved artifacts, board identity, and hardware test results |
| MOS conduction validation | SYS_CTRL2 readback proves AFE register state, not physical MOS conduction | No | Instrumented hardware fault/enable tests |
| IWDG timeout accuracy | STM32 LSI tolerance is a physical property | No | Measured target watchdog timing |
| CAN peripheral binding | Repository has no target CAN BSP for this board | No | Reviewed BSP, transceiver, bitrate, and bus integration tests |
| Physical Flash erase/program | Safe runtime scheduling and power-loss behavior are not demonstrated | No | Target timing, erase/program, reset/power-loss, and endurance tests |
| Phase 8 real-hardware hard gate | Production NTC and AFE approval artifacts remain absent | No | Complete approved v2 artifact/approval/manifest evidence chain |

The implementation is appropriate for a learning-project simulation profile. It is not production certification, a hardware gate pass, or proof of physical pack safety.
