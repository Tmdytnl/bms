# BMS V1 Simulation Release Candidate Report

Task: `BMS-SIM-RC-001`

Profile: `SIM-HW-POLICY-V1` / `SIM_POLICY_V1`

Result: **PASS — SIMULATION RELEASE CANDIDATE**

Evidence boundary: software simulation, deterministic fault injection, static
checks, and ARMCC5 target build only. No physical-hardware validation or
production certification is claimed.

## 1. Release-candidate scope

- Phase 8 simulation integration: centralized policy, AFE startup, NTC
  conversion, current mapping, SampleTask, measurement provenance, and lower
  phase regressions remain integrated.
- Phase 9 safety loop: authoritative Protect/State snapshots, directional
  software protection, single-writer FET management, phaseful XREADY recovery,
  generation-bound calibration, source-specific HW recovery, task health, and
  StateTask-owned IWDG decisions remain integrated.
- SOC and balancing: integer SOC integration/correction and sole-owner
  CELLBAL control with readback and fail-safe all-off remain integrated.
- Target CAN binding: PA11/PA12 bxCAN, PCLK1 36 MHz, 500 kbit/s at 87.5%
  sample point, exact `0x280` Standard-ID FIFO0 filter, priority-7 drain ISR,
  single CANTxTask hardware path, bus-off handling, and one-second init retry
  are implemented. The six diagnostic frames remain `0x180..0x185`.
- Physical persistence: record model v2 uses a 34-byte explicit encoding with
  CRC32 and a final 16-bit commit marker. The inactive 1 KiB bank is erased,
  the 32-byte body is programmed/read back first, commit is programmed last,
  and the old bank remains authoritative until full post-commit verification.
  Boot selects the newest valid wrap-safe sequence and restores SOC/capacity;
  Task_SOC is the low-frequency save owner.
- Debug UART completion: required USART1 PA9/PA10, 115200 8N1 binding is in the
  target and compiled through the repository SPL.

## 2. Deterministic scenario result

| Suite | Completed | Failed | Coverage |
|---|---:|---:|---|
| Phase 9 core | 24 | 0 | state/protection, FET, recovery, health, HW handshake |
| Continuation | 8 | 0 | SOC, balance, CAN, codec, physical A/B interruption and CRC fallback |
| Targeted races | 3 | 0 | Protect revision, XREADY handoff generation, HW recovery qualification |

Scenario result: **32/32 PASS**, plus **3/3 targeted races PASS**.

The two new persistence scenarios prove in simulated storage that interruption
at the commit-last halfword retains the previous bank and that corruption of a
newer committed bank falls back to the older valid bank.

## 3. Long-duration randomized fault simulation

The ARMCC5/Keil Simulator executes production state, SOC, balance, persistence,
and policy code with a fixed reproducible PRNG seed.

| Metric | Result |
|---|---:|
| Randomized iterations | 50,000 |
| Simulated state time | 600,000,000 ms (about 6.94 days) |
| Injected measurement/readiness/health events | 6,601 |
| Randomized A/B persistence transactions | 512 |
| Random power-cut program ordinals | 381 |
| Failures | 0 |

Checked invariants include defined fault bits only, measurement identity
binding, fail-closed readiness, directional fault inhibits, balance bitmap and
adjacency limits, SOC bounds, commit-last reboot selection, sequence monotonicity,
and preservation of the previous valid bank across every injected power cut.

## 4. Regression and build evidence

- Phase 4/6/7 and Phase 8 data/sample/AFE split ARMCC5 simulator regressions:
  **PASS**, all reported failures `0`.
- Phase 9/continuation/stress ARMCC5 simulator image: **PASS**, all reported
  failures `0`.
- Phase 9 simulation-development verifier: **PHASE9 SIMULATION GATE PASS**.
- Phase 8 artifact/manifest trust-chain suite: **49 tests PASS**.
- Production Keil/ARMCC5 5.06u7 Clean/Rebuild: **0 errors, 0 warnings**.
- Production size after target CAN, physical Flash, and UART binding:
  Code `51,236`, RO data `1,056`, RW data `352`, ZI data `15,408` bytes.
  Static RW+ZI RAM is `15,760` bytes, below the 20 KiB device limit; the
  application remains below the `0xF400` (61 KiB) IROM boundary.

Primary evidence:

- `firmware/Tests/Build/Phase8/phase8_split_simulator.log`
- `firmware/Project/Keil/Build/BMS_V1_Phase8_build.log`
- `firmware/Tests/Build/Phase8/BMS_V1_Phase8_production.map`
- `firmware/Tests/Build/Phase9/phase9_build.log`
- `firmware/Tests/Build/Phase9/phase9_simulator.log`

## 5. Safety ownership checks

- State classification is not generic FET permission.
- Protect remains sole runtime XREADY W1C owner.
- FET Manager remains sole scheduler-era SYS_CTRL2 CHG/DSG writer.
- Balance remains sole scheduler-era CELLBAL writer.
- `BMS_Data` remains diagnostic-only for FET authority.
- StateTask remains sole IWDG starter/feeder.
- CAN and Flash have no direct FET, CELLBAL, fault-bitmap, or IWDG authority.
- No generic clear-all-latched-fault API was introduced.
- Flash operations hold neither the data mutex nor the I2C mutex, never erase
  the newest valid bank first, and never feed the watchdog from Task_SOC.

## 6. Non-RC hardware gates

| Item | Simulation status | Required physical evidence |
|---|---|---|
| NTC/AFE production parameters | Real-hardware gate remains blocked | Approved immutable v2 artifacts and board identity |
| MOS conduction | Not claimed | Instrumented fault/enable tests |
| IWDG timing | Nominal software configuration only | Measured LSI and reset timing |
| CAN electrical bus | MCU binding built; physical bus unverified | Transceiver, termination, load, bus-off, EMC/ESD tests |
| Flash physical qualification | Transaction code built; silicon behavior unverified | Erase/program timing, brownout, endurance, watchdog and interrupt-latency tests |

The frozen `verify_phase8.py` real-hardware/evidence gate is intentionally not
weakened. It still returns its historical phase-boundary failures and the two
unconditional missing-artifact blockers on this later simulation-integrated
tree. This does not convert simulation evidence into a hardware PASS.
