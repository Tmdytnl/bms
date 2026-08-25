# BMS V1 Software Release Baseline

## Identity

| Field | Baseline |
|---|---|
| Project | BMS V1 learning/engineering project |
| Release type | Engineering Closure M3 — software/simulation baseline |
| Branch | `codex/phase8-phase9` |
| Starting baseline | `cdd8f9e59a2cab1fca4f1ba518b8e4aa4b7838b9` |
| Final content commit | `M3_FINAL_CONTENT_COMMIT` — filled in the final metadata commit |
| Date | 2026-08-25 (Asia/Shanghai) |
| Toolchain | Keil MDK5/uVision 5.38; ARMCC5 5.06 update 7 build 960 |
| MCU assumption | STM32F103C8T6, 64 KiB Flash, 20 KiB SRAM |
| AFE | BQ7694003, 13-series-cell configuration |
| Policy profile | `SIM-HW-POLICY-V1` / `SIM_POLICY_V1` |

This document identifies the software content baseline. The final metadata
commit records its parent content commit to avoid the impossible requirement
for a Git commit to contain its own hash. Repository final HEAD and push state
are reported by the M3 final execution report.

## Feature baseline

- bounded clock, GPIO, TIM3 and software-I2C BSP;
- BQ7694003 CRC transport, startup, configuration, calibration, 13S sampling,
  current/pack-voltage conversion and SIM-profile NTC conversion;
- coherent measurement snapshots with validity, freshness, sample sequence and
  AFE-generation identity;
- frozen seven-task FreeRTOS architecture with bounded waits, queues, mutexes,
  semaphore, event group, notification and fatal hooks;
- separate Protect/State authoritative safety snapshots, directional CHG/DSG
  inhibits and compare-and-publish State decisions;
- phaseful XREADY recovery, generation-bound calibration, source-specific HW
  recovery handshakes and Protect-only runtime XREADY W1C;
- sole scheduler-era FET Manager SYS_CTRL2 writer with revision/readback state;
- per-task monotonic heartbeat generations and StateTask-only IWDG feed;
- integer SOC engine and sole-owner balance/CELLBAL implementation;
- six explicit standard-ID CAN frames `0x180..0x185`, service RX `0x280`,
  STM32 bxCAN PA11/PA12 binding at 500 kbit/s and bounded ISR/task ownership;
- 34-byte Flash A/B persistence record, CRC32, wrap-safe selection,
  commit-last programming, readback verification and page restriction;
- USART1 PA9/PA10 115200 8N1 read-only 1 s bring-up snapshot with an 8 B per
  service bounded drain, including state,
  measurement identity/freshness, faults/inhibits, FET/recovery/health, SOC,
  balance, CAN, Flash and current/minimum-ever-free RTOS heap;
- architecture, ownership, runtime, bring-up, validation, debugging,
  configuration, resource, learning and interview documentation.

## Simulation evidence

| Evidence | Final M3 result |
|---|---|
| deterministic scenarios | 32/32 PASS (24 Phase 9 core + 8 continuation) |
| targeted races | 3/3 PASS |
| randomized stress | 50,000 iterations; 600,000,000 ms (~6.94 days); 6,601 injected events; 512 persistence transactions; 381 power cuts; 0 failures |
| lower production-C simulator regression | Phase 4/6/7 + Phase 8 data/sample/AFE split suites PASS; all failures 0 |
| Phase 8 trust-chain tests | 49/49 PASS |
| Phase 9 verifier | `PHASE9 SIMULATION GATE PASS`; `HARDWARE_CLAIM=NONE` |
| production Clean/Rebuild | 0 errors / 0 warnings |

Authoritative current release status is the Simulation RC report plus this M3
baseline. The earlier Simulation Integration report is historical.

## Resource baseline

| Item | Final ARMCC5 result |
|---|---:|
| Code | 53,344 B |
| RO data | 1,092 B |
| RW data | 364 B |
| ZI data | 16,508 B |
| RW + ZI | 16,872 B |
| application load image | 54,800 B (`0xD610`) |
| application Flash region | `[0x08000000, 0x0800F400)` = 62,464 B |
| Flash headroom before reserved pages | 7,664 B (12.27%) |
| SRAM limit / linked remaining | 20,480 B / 3,608 B |

The 12 KiB `ucHeap`, 1 KiB MSP and 512 B C heap are already included in linked
RW+ZI. Application, Idle and Timer stacks plus dynamic RTOS objects consume the
reserved `ucHeap` at runtime; they must not be added to RW+ZI again. A
configuration-matched estimate gives about 2,416 B heap remaining immediately
after scheduler startup. This is `STATIC/CONFIGURATION REVIEW ONLY`; `REAL
TARGET STACK WATERMARK VALIDATION REQUIRED`. See
`firmware/Project/SRAM_Budget.md` for the byte-level accounting.

The linker excludes the final three 1 KiB pages: SOC `0x0800F400`, parameter A
`0x0800F800`, and parameter B `0x0800FC00`. The M3 link result shows no overlap.

## Hardware validation boundary

The following are **DEFERRED — REAL_HW / HARDWARE VALIDATION REQUIRED**:

- real NTC curve, tolerance and temperature accuracy;
- real Rsense/current polarity, gain, offset and temperature drift;
- cell/pack-voltage accuracy and physical OV/UV/OCD/SCD thresholds/actions;
- ALERT and software-I2C electrical timing/waveforms;
- SYS_CTRL2 readback-to-physical MOS conduction and FET turn-on/off behavior;
- transceiver, termination, loading, waveform, bus-off and physical CAN timing;
- IWDG LSI frequency and reset timing;
- Flash erase/program/brownout/endurance behavior on the fitted MCU;
- target task stack watermarks, heap minimum, execution latency and ISR nesting;
- EMI, ESD, thermal and power-integrity behavior.

`REAL_HW validation has not been performed by this Milestone.`

## Known limitations

- `SIM_POLICY_V1` is a learning/simulation input, not an approved production
  parameter artifact.
- The frozen Phase 8 qualification gate remains blocked by missing approved v2
  NTC and AFE artifacts; simulation evidence does not resolve those blockers.
- UART is observability-only: there is no CLI, dynamic safety configuration,
  latch-clear command, FET-enable command or ownership bypass.
- Static callgraph margins and calculated heap remainder are not runtime
  watermarks.
- No production certification, safety integrity level, vehicle qualification,
  manufacturing readiness or hardware approval is claimed.

## Release conclusion

**PASS — SOFTWARE / SIMULATION RELEASE BASELINE**

**READY FOR REAL HARDWARE BRING-UP**

This is not a production release and not a hardware-certified baseline.
