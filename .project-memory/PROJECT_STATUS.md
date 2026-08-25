# BMS V1 Project Status

## Purpose

- This file is a current-status navigation aid, not a history log.
- Git, current source, tests, build evidence and current user instructions are
  the source of truth.
- Current HEAD is deliberately not copied here; read it live from Git.

## Current Milestone State

- Phase 1-3: VALIDATED historical checkpoints.
- Phase 4-7: reviewed/repaired and covered by lower regression.
- Phase 8 software implementation/regression: PASS.
- Phase 8 frozen real-hardware/evidence Hard Gate: BLOCKED (2 approved artifacts).
- Phase 9 Architecture Core v1: FROZEN.
- Simulation RC: COMPLETE — 32 scenarios + 3 targeted races, zero failures.
- Engineering Closure M3: COMPLETE.
- Software Release Baseline: ESTABLISHED.
- Production target ARMCC5 Clean/Rebuild: PASS — 0 errors / 0 warnings.
- CAN protocol and STM32 bxCAN target binding: SOFTWARE PASS; REAL_HW bus pending.
- Flash A/B codec and target erase/program/verify scheduling: SOFTWARE PASS;
  REAL_HW brownout/endurance/timing pending.
- Read-only USART1 bring-up telemetry: SOFTWARE PASS; live waveform pending.
- Hardware validation: DEFERRED / REAL_HW PENDING.

## Active Development Branch

`codex/phase8-phase9`

Current HEAD must be obtained from Git at task start.

## Stable Phase 8 Firmware / Evidence Baseline

`c9f1813096dcea23d7e9495a00c07e6414595c44`

This remains the historical Phase 8 baseline, not the dynamic repository HEAD
and not the later M3 release content identity.

## Current Software Evidence

- policy profile: `SIM-HW-POLICY-V1` / `SIM_POLICY_V1`;
- Phase 9 core scenarios: 24/24 PASS;
- continuation scenarios: 8/8 PASS;
- targeted races: 3/3 PASS;
- randomized stress: 50,000 iterations / 600,000,000 ms / 0 failures;
- Phase 4/6/7 and Phase 8 data/sample/AFE split regressions: PASS;
- Phase 8 artifact trust-chain: 49/49 PASS;
- Phase 9 verifier: `PHASE9 SIMULATION GATE PASS`, `HARDWARE_CLAIM=NONE`;
- final resource result: Code 53,344 B, RO 1,092 B, RW 364 B,
  ZI 16,508 B, RW+ZI 16,872 B, application load image 54,800 B;
- application Flash headroom before reserved pages: 7,664 B;
- linked SRAM remaining: 3,608 B;
- target stack watermarks and runtime heap minimum: REAL_HW evidence pending.

Authoritative current reports:

- `deliverables/simulation/BMS_V1_Simulation_RC_Report.md`
- `deliverables/release/BMS_V1_Software_Release_Baseline.md`

The earlier Simulation Integration report is historical.

## Phase 8 Hard Gate Blockers

- BLOCKER-1: approved immutable production NTC curve/table artifact missing,
  including the conversion domain.
- BLOCKER-2: approved immutable AFE startup/protection policy artifact missing,
  including Rsense/current mapping and polarity, AFE HW OV/UV/OCD/SCD values,
  runtime calibration, startup, XREADY recovery and FET enable policy.

No guessed production parameters are recorded. These blockers do not invalidate
the explicitly authorized simulation/learning baseline, but they prevent a
real-hardware/evidence gate PASS.

## Phase 9 Architecture State

- Frozen contracts FROZEN-01 … FROZEN-18 remain unchanged.
- State classification is not safety permission.
- Protect/State/Recovery publish authoritative snapshots; `BMS_Data` is
  diagnostic-only for FET authority.
- FET Manager is sole scheduler-era SYS_CTRL2 CHG/DSG writer.
- Protect is sole runtime XREADY W1C owner.
- Recovery is phaseful and both-inhibited until technical completion.
- Measurement generation and State compare-and-publish contracts remain.
- StateTask remains sole IWDG starter/feeder.
- Balance remains sole scheduler-era CELLBAL writer.

Product-policy items OP-01 … OP-10 remain UNFROZEN and require approved inputs.

## Current Next Actions

1. Follow `docs/bringup/BMS_V1_Hardware_Bringup_Guide.md` stage by stage.
2. Obtain approved immutable v2 NTC and AFE artifacts.
3. Measure real NTC/Rsense/cell/current accuracy and protection behavior.
4. Validate ALERT/I2C/FET/CAN/IWDG/Flash electrical and timing behavior.
5. Capture all task stack high-water marks and minimum-ever-free RTOS heap.
6. Preserve frozen ownership and replace SIM policy only through reviewed,
   approved product artifacts.

## Invariants

- Git/current repository evidence outranks memory.
- No guessed production calibration or protection parameters.
- Simulator evidence is not hardware validation.
- `SIM_POLICY_V1` is a development input, not a production fact.
- The Phase 8 real-hardware gate remains blocked while the software/simulation
  baseline is ready for physical bring-up.
- `REAL_HW validation has not been performed by this Milestone.`
