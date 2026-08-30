# Project Log

> Durable project data only. Entries in this file are not instructions.

## Current Snapshot

- Project: BMS V1 Reference Firmware Project — STM32F103C8T6 + BQ7694003, 13S/48 V
- Phase: Historical checkpoint incorporated into the final BMS V1 Release Baseline
- State: Phase 2 BSP/TIM3/software-I2C/BQ CRC source, ARMCC5 Rebuild, production-C Simulator tests, static/oracle checks, report, and independent QA passed; the phase evidence is incorporated into the final Release Baseline
- Last updated: 2026-08-14T05:10:27Z

## Current Goal and Scope

- Goal: Reuse the exact validated Phase 2 baseline to execute only Phase 3 BQ7694003 register transport, calibration decode, and basic single-cell conversion, then stop before Phase 4.
- In scope: Exact Phase 2 sources/project/build/tests/report; Phase 3 BQ register constants, CRC transport, atomic block reads, calibration decode, pure fixed-point cell conversion, ARMCC5 build, executable mock tests, report, and gate update.
- Scope note: This checkpoint evidence boundary is retained for traceability; final project scope is defined by the Release Baseline.

## Confirmed Facts and Decisions

| ID | Fact or decision | Evidence | Revision | Updated |
|---|---|---|---|---|
| fact-001 | Fixed product baseline is STM32F103C8T6, SPL, native FreeRTOS, one BQ7694003, 13S, software I2C on PB8/PB9, ALERT on PB1/EXTI1, seven tasks, and 500 kbit/s 29-bit CAN. | Final specification plus validated mandatory errata and gate document. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-002 | Toolchain is locked to Keil MDK5 and ARMCC5 V5.06 update 7 build 960 with STM32F10X_MD, USE_STDPERIPH_DRIVER, startup_stm32f10x_md.s, SPL V3.5.0, current CMSIS, FreeRTOS V11.1.0 portable/RVDS/ARM_CM3, and heap_4.c. | User-confirmed external old-project build evidence plus repository static compatibility review; evidence does not claim the current BMS project builds. | sha256:8475ac7507ef42dacc4702de68be9df59634812d43c21006e91acde071c2ef4f | 2026-08-13T14:19:15Z |
| fact-003 | BMS FreeRTOS baseline is configMAX_PRIORITIES=8, initial heap target about 8 KiB, configASSERT enabled, stack overflow check 2, 1 ms preemptive tick, and seven task priorities 5/4/3/3/2/2/2. | Validated software gate and errata; old 17 KiB config is a superseded reference. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-004 | NVIC baseline is 4 priority bits, PriorityGroup_4, library max syscall 5/raw 0x50, lowest/kernel raw 15/0xF0, EXTI1 logical 6, and CAN RX0 logical 7; FreeRTOS uniquely owns SVC/PendSV/SysTick. | Static STM32/FreeRTOS port review and validated gate. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-005 | ALERT uses task-level drain/retry; SYS_STAT bits are handled independently; CC_READY clears only after the newest sample enters the queue path; XREADY requires full recovery before clear; OVRD_ALERT is explicit; CHG/DSG uses a permission-based single writer. | Validated mandatory errata C/H closure matrix. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-006 | CAN safety commands are unicast only; broadcast is read-only/discovery/telemetry class. CAN application CRC is CRC-8/ATM poly 0x07, init 0, refin/refout false, xorout 0, with 123456789 mapping to 0xF4, and is separate from BQ CRC. | User decision captured and independently QA-validated in gate artifacts. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-007 | Final Flash layout is 61 KiB application region [0x08000000,0x0800F400), SOC log 0x0800F400-0x0800F7FF, Parameter A 0x0800F800-0x0800FBFF, and B 0x0800FC00-0x0800FFFF; parameter commit marker is the final persistent write and there is no persistent active flag. | Address arithmetic and ST Flash facts independently reviewed; mandatory errata validated. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-008 | StateTask is the system-health supervisor and sole application IWDG feeder; the final production policy binds nominal 4000 ms, with PR/RLR and LSI min/typ/max windows recorded by the BSP and interface evidence. | Validated mandatory errata and fixed seven-task architecture. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-009 | The checkpoint indexed PCB/BOM, NTC/OCV/Rsense calibration, MOS, balancing thermal, ALERT/WAKE, CAN, brownout, LSI timing, EMC/ESD, and safety-interface dimensions for controlled traceability. | Independently QA-validated gate verdict. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-010 | Generated documents use deliverables; future firmware source, Keil project/scatter and tests use firmware; docs remains input/reference material. | Explicit user decision captured in repository AGENTS.md. | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | 2026-08-13T13:40:42Z |
| fact-011 | The current Phase 1 BMS_V1 target reproducibly builds with ARMCC5 V5.06 update 7 build 960 at 0 errors and 0 warnings; Program Size is Code=812, RO-data=252, RW-data=0, ZI-data=1856. | Current plain-text Rebuild log, map/scatter inspection, independent fromelf totals, static verifier, and independent QA. | sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26 | 2026-08-13T15:15:29Z |
| fact-012 | The Phase 1 application link region is IROM 0x08000000+0xF400 and IRAM 0x20000000+0x5000; actual load size is 0x428 and actual RW/ZI region size is 0x740. | Current ARMCC5-generated scatter and map; map SHA-256 4751ffdb819e32c201b61f1badb9e32e6dc402759ef3d7c68d1b1fe82533ac67. | sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a | 2026-08-13T15:15:29Z |
| fact-013 | Phase 2 verifies RCC ON/READY/source/dividers and 72/72/36/72 MHz before later hardware init; TIM3 is 72 MHz/PSC71/ARRFFFF for a 1 MHz free-running counter. | Source and ST review, executable wrap tests, independent QA PASS. | sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34 | 2026-08-14T05:10:27Z |
| fact-014 | Phase 2 software-I2C uses PB8/PB9 open-drain release/drive-low plus physical IDR readback, bounded SCL/bus waits, deferred read ACK/NACK, and cleanup-safe generic 9-clock-plus-STOP recovery. | Production C executed against the ARMCC5 Keil simulator mock with completed=1/failures=0; static/oracle verifier exit 0; independent QA PASS. | sha256:ef8e3c735f6bf3b7635d8234089aa9d33ae45bd614fc27cf7cc7485433186bb3 | 2026-08-14T05:10:27Z |
| fact-015 | The Phase 2 BMS_V1 target Clean+Rebuilds with ARMCC5 V5.06 update 7 build 960 at 0 errors/0 warnings; Program Size is Code=3084, RO-data=268, RW-data=24, ZI-data=1896. | Exact phase-specific Rebuild log, map/fromelf totals, verify_phase2 exit 0, and independent QA PASS. | sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79 | 2026-08-14T05:10:27Z |
| fact-016 | BQ CRC is MSB-first poly 0x07/init 0 with TI first/subsequent write/read framing; receive and ninth-clock response are separate. | CRC oracle, production-C simulator, TI Rev.I review, report QA. | sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34 | 2026-08-14T05:10:27Z |

## Active Artifacts

| ID | Path | Revision | Status | Gate | Purpose | Evidence | Reuse guidance | Updated |
|---|---|---|---|---|---|---|---|---|
| art-001 | deliverables/review/BMS_V1_开发准备报告.md | sha256:22cac90f445f8c420f27699c2a03ce50977cceb42209cf0b8d87457db788336c | USABLE | RECHECK | Historical preparation audit and original risk discovery. | A–J coverage and prior independent QA; its original findings are superseded by art-005/art-006 decisions. | Use as background evidence only; apply art-005 mandatory errata and art-006 current gate, use the later accepted disposition. | 2026-08-13T14:19:15Z |
| art-002 | docs/spec/BMS_V1_统一项目方案_软件设计规格.md | sha256:7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472 | USABLE | RECHECK | Level 1 project-intent specification. | Full 4,241-line review; known stale passages are explicitly overridden by art-005. | Reuse only together with exact art-005; untouched conflicting pseudo-code and values must not guide implementation. | 2026-08-13T14:19:15Z |
| art-003 | docs/FreeRTOS/FreeRTOSConfig.h | sha256:94cc2e9a1e520cdba7924c643c3d9dd4dd608f5c00b9f8de70fd6eec27a6030a | EXPERIMENTAL | BLOCK | Old FreeRTOS configuration reference. | Contains max priorities 5, 17 KiB heap, raw 0xBF, and disabled diagnostics; static audit confirmed incompatibility with the BMS baseline. | Do not copy into firmware; create a BMS-specific config from art-005/art-006 in the authorized RTOS phase. | 2026-08-13T14:19:15Z |
| art-004 | AGENTS.md | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | VALIDATED | ALLOW | Repository placement convention. | Explicit user decision and verified paths. | Apply placement convention unless the user changes it. | 2026-08-13T13:40:42Z |
| art-005 | deliverables/review/BMS_V1_规格勘误表.md | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | VALIDATED | ALLOW | Mandatory corrections and closed design decisions overriding conflicting specification passages. | All C-01/C-02/H-01-H-13 and ten additional convergence items covered; status enum, addresses, arithmetic, UTF-8, and evidence boundaries checked; independent final QA passed. | Always read with art-002 before implementation; reuse only this exact revision. | 2026-08-13T14:19:15Z |
| art-006 | deliverables/review/BMS_V1_Software_Implementation_Gate.md | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | VALIDATED | ALLOW | Authoritative Software/Hardware gate verdict and stable development baseline. | Static source review, 25-row errata validation, exact address/math checks, external-build evidence boundary, and independent final QA passed. | Use with art-005 as the current baseline; its Phase 1 entry decision is now fulfilled by art-007 through art-010. | 2026-08-13T14:19:15Z |
| art-010 | deliverables/phase1/BMS_V1_Phase1_Report.md | sha256:c444a4fe0db8401ba22b9515aef22d1c1e17f5057a872140698bc269859ea0b9 | VALIDATED | ALLOW | Phase 1 implementation, build, memory-budget, boundary, and handoff report. | Required 17 topics covered; UTF-8 clean; build/map values cross-checked; independent final QA PASS. | Use as the Phase 1 completion record within the final evidence chain; evidence remains bound to its stated build and map method. | 2026-08-13T15:15:29Z |
| art-011 | firmware | sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942 | VALIDATED | ALLOW | Phase 2 source manifest: six App, three Config, ten Driver, and User/main files. | Sorted path-NUL-file-SHA over 20 files; verify, Rebuild 0/0, simulator and QA PASS. | Reuse only if the same 20 files reproduce this revision; no Phase 3 transport is included. | 2026-08-14T05:10:27Z |
| art-012 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99 | VALIDATED | ALLOW | Phase 2 ARMCC5 target with exact MD memory/startup boundary, authorized BSP/CRC sources, and only RCC/GPIO/TIM SPL additions. | XML/static scan, full Clean+Rebuild 0/0, map/fromelf inspection, verify_phase2 exit 0, and independent QA PASS. | Use this exact project revision as the Phase 3 project baseline; add only Phase 3-authorized driver sources. | 2026-08-14T05:10:27Z |
| art-013 | firmware/Project/Keil/Build/BMS_V1_Phase2_build.log | sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79 | VALIDATED | ALLOW | Plain-text exact Phase 2 ARMCC5 Clean+Rebuild evidence. | ARMCC5 V5.06u7 build 960; every target unit rebuilt; 0 errors/0 warnings; Code=3084, RO=268, RW=24, ZI=1896. | Historical Phase 2 build baseline only; any source/project change requires a new phase-specific Rebuild log. | 2026-08-14T05:10:27Z |
| art-014 | firmware/Tests/Build/Phase2/phase2_simulator.log | sha256:ef8e3c735f6bf3b7635d8234089aa9d33ae45bd614fc27cf7cc7485433186bb3 | VALIDATED | ALLOW | Actual ARMCC5-compiled Phase 2 production soft-I2C/CRC execution result under Keil Cortex-M3 Simulator. | Log has completed=1/failures=0 and no debugger error; map confirms production objects linked; independent QA PASS. | Evidence bound to the recorded production-C Simulator trace and artifact identity. | 2026-08-14T05:10:27Z |
| art-015 | firmware/Tests/Build/Phase2/verify_phase2.log | sha256:3b78fdb340e5cadec61836bc6176ec7ad864cb9a36b3849dba3382648cccb364 | VALIDATED | ALLOW | Phase 2 independent CRC oracle and static/execution gate result. | verify_phase2.py exit 0 with all five groups PASS; exact test script revision 57744196bc5d2ca58e7b0b2168ec6d96106b181066eb61221dc88c5d5d577444. | Re-run after any Phase 2 baseline change; Python checks do not replace ARMCC5 build or actual-C simulator execution. | 2026-08-14T05:10:27Z |
| art-016 | deliverables/phase2/BMS_V1_Phase2_Report.md | sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34 | VALIDATED | ALLOW | Phase 2 implementation, tests, build, size delta, hardware boundary, and Phase 3 handoff report. | All 16 required topics, seven embedded artifact hashes, sizes and claims cross-checked; independent final QA PASS. | Use with art-011 through art-015 as the exact Phase 2 completion record within the final evidence chain. | 2026-08-14T05:10:27Z |

## Invalidated Artifact Tombstones

| ID | Path | Revision | Status | Gate | Reason | Replacement | Evidence | Updated |
|---|---|---|---|---|---|---|---|---|
| art-007 | firmware | sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d | SUPERSEDED | BLOCK | Phase 1 source manifest was intentionally extended and main/config changed by the authorized Phase 2 implementation. | art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942 | Exact prior Phase 1 revision remains documented by art-010; current 20-file manifest independently validated. | 2026-08-14T05:10:27Z |
| art-008 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a | SUPERSEDED | BLOCK | Phase 1 six-source project was intentionally extended by authorized Phase 2 driver and SPL sources. | art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99 | Current project XML and fresh Clean+Rebuild independently validated. | 2026-08-14T05:10:27Z |
| art-009 | firmware/Project/Keil/Build/BMS_V1_build.log | sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26 | SUPERSEDED | BLOCK | The Phase 1 build log does not prove the Phase 2 source/project revision. | art-013@sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79 | Exact Phase 2 Clean+Rebuild log and sizes validated. | 2026-08-14T05:10:27Z |

## Checkpoint Closure

- No Phase 2 software blocker remains; Phase 2 is complete and its Hard Gate passed.
- Continuation: The subsequent phase evidence is incorporated into the final Release Baseline.
- The vendor SystemInit HSE-failure branch remains empty and its PLLRDY/SWS pre-main waits are not bounded by Phase 2; BSP readback only handles systems that reach main.
- Phase 4 sampling/13S mapping and all ALERT/protection/FET/balance/SOC/CAN/Flash/FreeRTOS/IWDG behavior remain out of scope.
- Interface register: PCB/BOM, calibration, power-stage, thermal, waveform, bus, brownout, timing, EMC/ESD, and safety dimensions are indexed for traceability.

## Recent Task History

### 2026-08-14T05:10:27Z | phase2-gate-20260814 | Complete Phase 2 BSP, software-I2C, CRC, and hard gate

- Request: Execute Phase 2 first, including MCU BSP, TIM3, bounded software-I2C, BQ CRC, tests, ARMCC5 build, report, project-log checkpoint, and hard gate; begin Phase 3 only after exact PASS.
- Outcome: Implemented and validated Phase 2 outside docs; corrected the drifted ARMCC5 project lock; completed a full Clean+Rebuild at 0 errors/0 warnings; executed actual production soft-I2C/CRC C under the Keil simulator with completed=1/failures=0; Phase 2 Hard Gate passed.
- Artifacts: art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942; art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99; art-013@sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79; art-014@sha256:ef8e3c735f6bf3b7635d8234089aa9d33ae45bd614fc27cf7cc7485433186bb3; art-015@sha256:3b78fdb340e5cadec61836bc6176ec7ad864cb9a36b3849dba3382648cccb364; art-016@sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34
- Validation: verify_phase2.py exit 0; Keil simulator actual-C harness completed=1/failures=0; ARMCC5 V5.06u7 build 960 Clean+Rebuild 0 errors/0 warnings with Code=3084, RO=268, RW=24, ZI=1896; map/fromelf and seven report hashes cross-checked; independent final QA PASS; no Phase 3 file existed at the checkpoint.
- Decisions: Deferred read response is frozen for CRC-aware ACK/NACK; generic 9-clock recovery is bounded but not a TI or hardware guarantee; Phase 3 may start only from the exact validated Phase 2 revision.
- Invalidated: art-007@sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d; art-008@sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a; art-009@sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
- Next: Create Phase 3 artifacts only after this exact log revision is committed and a final no-Phase3-file scan passes.

### 2026-08-13T15:15:29Z | phase1-baseline-20260813 | Complete Phase 1 engineering baseline and build boundary

- Request: Execute only BMS V1 Phase 1, creating the firmware directory baseline, public models, Keil/ARMCC5 target, compile-time and memory boundaries, static checks, completion report, and project-memory update; stop before Phase 2.
- Outcome: Created the Phase 1 source/project/test/report artifacts outside docs; completed a real ARMCC5 V5.06u7 Rebuild with 0 errors and 0 warnings; closed Phase 1 without implementing Phase 2 behavior.
- Artifacts: art-007@sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d; art-008@sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a; art-009@sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26; art-010@sha256:c444a4fe0db8401ba22b9515aef22d1c1e17f5057a872140698bc269859ea0b9
- Validation: verify_phase1.py exit 0; ARMCC5 Rebuild 0 errors/0 warnings with Code=812, RO=252, RW=0, ZI=1856; map/scatter boundaries checked; model test translation unit compiled with ARMCC5; independent final QA PASS; report UTF-8 replacement count 0.
- Decisions: Preserve the checkpoint evidence chain; approved policy binding and final disposition are incorporated into the Release Baseline.
- Invalidated: none
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
- Next: Wait for the user's explicit Phase 2 instruction.

### 2026-08-13T14:19:15Z | software-gate-close-20260813 | Close Software Implementation Gate and freeze the development baseline

- Request: Use the preparation report, specification, current repository, user-confirmed old ARMCC5 build evidence, and final decisions to create mandatory errata, distinguish Software and Hardware gates, persist the stable baseline, and establish the recorded entry checkpoint.
- Outcome: Created and independently QA-validated art-005 and art-006; closed or mitigated all prior software entry risks; Software Implementation Gate passed with no Phase 1 software blocker; the subsequent phase evidence is incorporated into the final Release Baseline.
- Artifacts: art-005@sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e; art-006@sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867
- Validation: 25 errata rows use allowed statuses with no OPEN; ARMCC5/RVDS, NVIC, Flash 61 KiB layout, 13S, CAN timing/CRC, A/B protocol, CC queue semantics and IWDG ownership were independently reviewed; UTF-8 clean; final QA PASS.
- Decisions: Keil MDK5/ARMCC5 and the RTOS stack were locked at this checkpoint; the phase evidence is incorporated into the final Release Baseline.
- Invalidated: none
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
- Next: Wait for the user's explicit Phase 1 instruction.

### 2026-08-13T13:42:32Z | finalize-output-layout-20260813 | Synchronize report content after relocation

- Request: Keep generated documents and future generated code outside docs and organize them separately.
- Outcome: Updated the relocated report repository tree so it accurately identifies docs as inputs and deliverables as generated output; no firmware directory or Phase 1 code was created.
- Artifacts: art-001@sha256:22cac90f445f8c420f27699c2a03ce50977cceb42209cf0b8d87457db788336c
- Validation: Report has all A–J content, 12 valid Phase rows, zero UTF-8 replacement characters, and the recorded SHA-256.
- Decisions: Directory placement convention remains docs for inputs, deliverables for generated documents, and firmware for future code.
- Invalidated: none
- Closure: The recorded entry findings were resolved in the subsequent phase chain and incorporated into the final Release Baseline.
- Next: User reviews art-001 and supplies the six decisions before Phase 1.

### 2026-08-13T13:40:42Z | separate-output-layout-20260813 | Separate generated documents and future firmware from docs

- Request: Do not place generated output documents or future generated code under docs; organize them separately.
- Outcome: Moved the unchanged preparation report to deliverables/review, removed the empty docs/review directory, and recorded the docs/deliverables/firmware placement convention in AGENTS.md.
- Artifacts: art-001@sha256:141207b8da251894eddb4a9c218b0c65171c50fde3752078a2ec94f195329b18; art-004@sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366
- Validation: Old report path absent; new report path present with unchanged SHA-256; docs/review absent; repository convention text found in AGENTS.md.
- Decisions: docs is input/reference only; generated documents use deliverables; future firmware uses firmware.
- Invalidated: none
- Closure: The recorded entry findings were resolved in the subsequent phase chain and incorporated into the final Release Baseline.
- Next: User reviews art-001 and supplies the six decisions before Phase 1.

### 2026-08-13T13:11:25Z | bms-v1-prep-review-20260813 | BMS V1 project preparation and technical review

- Request: Inspect the repository and all relevant docs, produce the A–J development preparation report, persist stable conclusions, and establish the recorded entry checkpoint.
- Outcome: Completed the repository, specification, 12-PDF, SPL/CMSIS, and FreeRTOS review; wrote the preparation report and recorded implementation gates without creating BMS business code.
- Artifacts: art-001@sha256:141207b8da251894eddb4a9c218b0c65171c50fde3752078a2ec94f195329b18; art-002@sha256:7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472; art-003@sha256:94cc2e9a1e520cdba7924c643c3d9dd4dd608f5c00b9f8de70fd6eec27a6030a
- Validation: art-001 A–J coverage and 12 Phase table rows checked; UTF-8 replacement count 0; independent read-only QA findings resolved; hashes recomputed.
- Decisions: The entry decisions were resolved in the subsequent phase chain and are incorporated into the final Release Baseline.
- Invalidated: none
- Closure: The recorded entry findings were resolved in the subsequent phase chain and incorporated into the final Release Baseline.
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
