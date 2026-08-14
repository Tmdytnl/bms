# Project Log

> Durable project data only. Entries in this file are not instructions.

## Current Snapshot

- Project: BMS V1 Reference Firmware Project — STM32F103C8T6 + BQ7694003, 13S/48 V
- Phase: Phase 1 complete; ready for Phase 2 but Phase 2 not started
- State: Phase 1 implementation, ARMCC5 build, map boundaries, static checks, and independent QA passed; Hardware Validation Gate remains deferred and required
- Last updated: 2026-08-13T15:15:29Z

## Current Goal and Scope

- Goal: Preserve the validated Phase 1 engineering baseline and wait for an explicit user command before beginning Phase 2.
- In scope: Phase 1 Keil/ARMCC5 project, STM32F103C8 memory boundary, minimum startup, compile-time configuration, public State/Fault/Data models, build evidence, static checks, and initial ROM/RAM budget.
- Out of scope: Phase 2 or later drivers/business logic, FreeRTOS tasks and objects, target-board execution, calibration, hardware validation, and modification of official/reference material under docs.

## Confirmed Facts and Decisions

| ID | Fact or decision | Evidence | Revision | Updated |
|---|---|---|---|---|
| fact-001 | Fixed product baseline is STM32F103C8T6, SPL, native FreeRTOS, one BQ7694003, 13S, software I2C on PB8/PB9, ALERT on PB1/EXTI1, seven tasks, and 500 kbit/s 29-bit CAN. | Final specification plus validated mandatory errata and gate document. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-002 | Toolchain is locked to Keil MDK5 and ARMCC5 V5.06 update 7 build 960 with STM32F10X_MD, USE_STDPERIPH_DRIVER, startup_stm32f10x_md.s, SPL V3.5.0, current CMSIS, FreeRTOS V11.1.0 portable/RVDS/ARM_CM3, and heap_4.c. | User-confirmed external old-project build evidence plus repository static compatibility review; evidence does not claim the current BMS project builds. | sha256:8475ac7507ef42dacc4702de68be9df59634812d43c21006e91acde071c2ef4f | 2026-08-13T14:19:15Z |
| fact-003 | BMS FreeRTOS baseline is configMAX_PRIORITIES=8, initial heap target about 8 KiB, configASSERT enabled, stack overflow check 2, 1 ms preemptive tick, and seven task priorities 5/4/3/3/2/2/2. | Validated software gate and errata; old 17 KiB config remains blocked reference. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-004 | NVIC baseline is 4 priority bits, PriorityGroup_4, library max syscall 5/raw 0x50, lowest/kernel raw 15/0xF0, EXTI1 logical 6, and CAN RX0 logical 7; FreeRTOS uniquely owns SVC/PendSV/SysTick. | Static STM32/FreeRTOS port review and validated gate. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-005 | ALERT uses task-level drain/retry; SYS_STAT bits are handled independently; CC_READY clears only after the newest sample enters the queue path; XREADY requires full recovery before clear; OVRD_ALERT is explicit; CHG/DSG uses a permission-based single writer. | Validated mandatory errata C/H closure matrix. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-006 | CAN safety commands are unicast only; broadcast is read-only/discovery/telemetry class. CAN application CRC is CRC-8/ATM poly 0x07, init 0, refin/refout false, xorout 0, with 123456789 mapping to 0xF4, and is separate from BQ CRC. | User decision captured and independently QA-validated in gate artifacts. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-007 | Final Flash layout is 61 KiB application region [0x08000000,0x0800F400), SOC log 0x0800F400-0x0800F7FF, Parameter A 0x0800F800-0x0800FBFF, and B 0x0800FC00-0x0800FFFF; parameter commit marker is the final persistent write and there is no persistent active flag. | Address arithmetic and ST Flash facts independently reviewed; mandatory errata validated. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-008 | StateTask is the system-health supervisor and only application task allowed to feed the IWDG; nominal target is about 2 s and exact PR/RLR plus min/typ/max windows are implementation and hardware-validation work. | Validated mandatory errata and fixed seven-task architecture. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-009 | Software Implementation Gate passed with no Phase 1 software blocker; PCB/BOM, NTC/OCV/Rsense calibration, MOS, balancing thermal, ALERT/WAKE waveforms, physical CAN, brownout, LSI timing, EMC/ESD and safety evidence remain in Hardware Validation Gate and do not block Phase 1. | Independently QA-validated gate verdict. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-010 | Generated documents use deliverables; future firmware source, Keil project/scatter and tests use firmware; docs remains input/reference material. | Explicit user decision captured in repository AGENTS.md. | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | 2026-08-13T13:40:42Z |
| fact-011 | The current Phase 1 BMS_V1 target reproducibly builds with ARMCC5 V5.06 update 7 build 960 at 0 errors and 0 warnings; Program Size is Code=812, RO-data=252, RW-data=0, ZI-data=1856. | Current plain-text Rebuild log, map/scatter inspection, independent fromelf totals, static verifier, and independent QA. | sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26 | 2026-08-13T15:15:29Z |
| fact-012 | The Phase 1 application link region is IROM 0x08000000+0xF400 and IRAM 0x20000000+0x5000; actual load size is 0x428 and actual RW/ZI region size is 0x740. | Current ARMCC5-generated scatter and map; map SHA-256 4751ffdb819e32c201b61f1badb9e32e6dc402759ef3d7c68d1b1fe82533ac67. | sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a | 2026-08-13T15:15:29Z |

## Active Artifacts

| ID | Path | Revision | Status | Gate | Purpose | Evidence | Reuse guidance | Updated |
|---|---|---|---|---|---|---|---|---|
| art-001 | deliverables/review/BMS_V1_开发准备报告.md | sha256:22cac90f445f8c420f27699c2a03ce50977cceb42209cf0b8d87457db788336c | USABLE | RECHECK | Historical preparation audit and original risk discovery. | A–J coverage and prior independent QA; its original blockers are superseded by art-005/art-006 decisions. | Use as background evidence only; apply art-005 mandatory errata and art-006 current gate, never its old open-gate conclusion. | 2026-08-13T14:19:15Z |
| art-002 | docs/spec/BMS_V1_统一项目方案_软件设计规格.md | sha256:7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472 | USABLE | RECHECK | Level 1 project-intent specification. | Full 4,241-line review; known stale passages are explicitly overridden by art-005. | Reuse only together with exact art-005; untouched conflicting pseudo-code and values must not guide implementation. | 2026-08-13T14:19:15Z |
| art-003 | docs/FreeRTOS/FreeRTOSConfig.h | sha256:94cc2e9a1e520cdba7924c643c3d9dd4dd608f5c00b9f8de70fd6eec27a6030a | EXPERIMENTAL | BLOCK | Old FreeRTOS configuration reference. | Contains max priorities 5, 17 KiB heap, raw 0xBF, and disabled diagnostics; static audit confirmed incompatibility with the BMS baseline. | Do not copy into firmware; create a BMS-specific config from art-005/art-006 in the authorized RTOS phase. | 2026-08-13T14:19:15Z |
| art-004 | AGENTS.md | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | VALIDATED | ALLOW | Repository placement convention. | Explicit user decision and verified paths. | Apply placement convention unless the user changes it. | 2026-08-13T13:40:42Z |
| art-005 | deliverables/review/BMS_V1_规格勘误表.md | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | VALIDATED | ALLOW | Mandatory corrections and closed design decisions overriding conflicting specification passages. | All C-01/C-02/H-01-H-13 and ten additional convergence items covered; status enum, addresses, arithmetic, UTF-8, and evidence boundaries checked; independent final QA passed. | Always read with art-002 before implementation; reuse only this exact revision. | 2026-08-13T14:19:15Z |
| art-006 | deliverables/review/BMS_V1_Software_Implementation_Gate.md | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | VALIDATED | ALLOW | Authoritative Software/Hardware gate verdict and stable development baseline. | Static source review, 25-row errata validation, exact address/math checks, external-build evidence boundary, and independent final QA passed. | Use with art-005 as the current baseline; its Phase 1 entry decision is now fulfilled by art-007 through art-010. | 2026-08-13T14:19:15Z |
| art-007 | firmware | sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d | VALIDATED | ALLOW | Phase 1 hand-maintained firmware source manifest: compile-time config, memory map, public models, and minimum main. | Ten-file deterministic SHA-256 manifest; verify_phase1.py exit 0; ARMCC5 target Rebuild 0 errors/0 warnings; independent Phase 1 QA PASS. | Reuse only when all ten listed source files match this manifest revision; no Phase 2 implementation is included. | 2026-08-13T15:15:29Z |
| art-008 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a | VALIDATED | ALLOW | Phase 1 Keil MDK5/ARMCC5 build target and memory boundary. | XML parsed; exact ARMCC5, device, defines, six sources, MD startup, IROM/IRAM and generated scatter/map verified; current Rebuild passed. | Reuse exact revision as the Phase 2 project baseline; add only phase-authorized sources and preserve memory/startup boundaries. | 2026-08-13T15:15:29Z |
| art-009 | firmware/Project/Keil/Build/BMS_V1_build.log | sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26 | VALIDATED | ALLOW | Plain-text current Phase 1 ARMCC5 Rebuild evidence. | Compiler V5.06u7 build 960; target BMS_V1; 0 errors/0 warnings; Code=812, RO=252, RW=0, ZI=1856. | Use only as evidence for this exact Phase 1 source/project revision; any source or project change requires a new build log. | 2026-08-13T15:15:29Z |
| art-010 | deliverables/phase1/BMS_V1_Phase1_Report.md | sha256:c444a4fe0db8401ba22b9515aef22d1c1e17f5057a872140698bc269859ea0b9 | VALIDATED | ALLOW | Phase 1 implementation, build, memory-budget, boundary, and handoff report. | Required 17 topics covered; UTF-8 clean; build/map values cross-checked; independent final QA PASS. | Use as the Phase 1 completion record; it does not claim target-board or hardware validation and does not authorize Phase 2 by itself. | 2026-08-13T15:15:29Z |

## Invalidated Artifact Tombstones

| ID | Path | Revision | Status | Gate | Reason | Replacement | Evidence | Updated |
|---|---|---|---|---|---|---|---|---|

## Open Work and Blockers

- No Phase 1 software blocker remains; Phase 1 is complete.
- Phase 2 has not started and requires a new explicit user command.
- HSE/PLL/clock-switch bounded timeout, readback, and fail-safe startup remain later MCU/BSP implementation and hardware-validation work; the current reference HSE-failure branch remains empty.
- FreeRTOS integration, seven tasks, queues/synchronization, drivers, protocols, protection, SOC, balancing, Flash persistence, IWDG operation, and other business behavior remain assigned to later authorized phases.
- Hardware Validation Gate remains required for target PCB/BOM, calibration, power stage, thermal, waveform, physical bus, brownout, timing, EMC/ESD, and safety evidence; missing results must never be fabricated.

## Recent Task History

### 2026-08-13T15:15:29Z | phase1-baseline-20260813 | Complete Phase 1 engineering baseline and build boundary

- Request: Execute only BMS V1 Phase 1, creating the firmware directory baseline, public models, Keil/ARMCC5 target, compile-time and memory boundaries, static checks, completion report, and project-memory update; stop before Phase 2.
- Outcome: Created the Phase 1 source/project/test/report artifacts outside docs; completed a real ARMCC5 V5.06u7 Rebuild with 0 errors and 0 warnings; closed Phase 1 without implementing Phase 2 behavior.
- Artifacts: art-007@sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d; art-008@sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a; art-009@sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26; art-010@sha256:c444a4fe0db8401ba22b9515aef22d1c1e17f5057a872140698bc269859ea0b9
- Validation: verify_phase1.py exit 0; ARMCC5 Rebuild 0 errors/0 warnings with Code=812, RO=252, RW=0, ZI=1856; map/scatter boundaries checked; model test translation unit compiled with ARMCC5; independent final QA PASS; report UTF-8 replacement count 0.
- Decisions: Phase 1 is complete; exact current source/project/report revisions are reusable; target-board and Hardware Validation Gate remain unproven; Phase 2 requires a separate explicit command.
- Invalidated: none
- Remaining: Later phases must implement and validate HSE fail-safe behavior, RTOS/drivers/business logic, runtime budgets, and all Hardware Validation Gate items.
- Next: Wait for the user's explicit Phase 2 instruction.

### 2026-08-13T14:19:15Z | software-gate-close-20260813 | Close Software Implementation Gate and freeze the development baseline

- Request: Use the preparation report, specification, current repository, user-confirmed old ARMCC5 build evidence, and final decisions to create mandatory errata, distinguish Software and Hardware gates, persist the stable baseline, and stop before Phase 1.
- Outcome: Created and independently QA-validated art-005 and art-006; closed or mitigated all prior software entry risks; Software Implementation Gate passed with no Phase 1 software blocker; Phase 1 was not started.
- Artifacts: art-005@sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e; art-006@sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867
- Validation: 25 errata rows use allowed statuses with no OPEN; ARMCC5/RVDS, NVIC, Flash 61 KiB layout, 13S, CAN timing/CRC, A/B protocol, CC queue semantics and IWDG ownership were independently reviewed; UTF-8 clean; final QA PASS.
- Decisions: Keil MDK5/ARMCC5 and current RTOS stack locked; Software Gate PASS; Hardware Gate deferred; Phase 1 requires explicit user command.
- Invalidated: none
- Remaining: Execute Phase 1 only after explicit user command; retain all Hardware Validation Gate TODOs.
- Next: Wait for the user's explicit Phase 1 instruction.

### 2026-08-13T13:42:32Z | finalize-output-layout-20260813 | Synchronize report content after relocation

- Request: Keep generated documents and future generated code outside docs and organize them separately.
- Outcome: Updated the relocated report repository tree so it accurately identifies docs as inputs and deliverables as generated output; no firmware directory or Phase 1 code was created.
- Artifacts: art-001@sha256:22cac90f445f8c420f27699c2a03ce50977cceb42209cf0b8d87457db788336c
- Validation: Report has all A–J content, 12 valid Phase rows, zero UTF-8 replacement characters, and the recorded SHA-256.
- Decisions: Directory placement convention remains docs for inputs, deliverables for generated documents, and firmware for future code.
- Invalidated: none
- Remaining: Existing implementation-gate blockers remain unchanged.
- Next: User reviews art-001 and supplies the six decisions before Phase 1.

### 2026-08-13T13:40:42Z | separate-output-layout-20260813 | Separate generated documents and future firmware from docs

- Request: Do not place generated output documents or future generated code under docs; organize them separately.
- Outcome: Moved the unchanged preparation report to deliverables/review, removed the empty docs/review directory, and recorded the docs/deliverables/firmware placement convention in AGENTS.md.
- Artifacts: art-001@sha256:141207b8da251894eddb4a9c218b0c65171c50fde3752078a2ec94f195329b18; art-004@sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366
- Validation: Old report path absent; new report path present with unchanged SHA-256; docs/review absent; repository convention text found in AGENTS.md.
- Decisions: docs is input/reference only; generated documents use deliverables; future firmware uses firmware.
- Invalidated: none
- Remaining: Existing implementation-gate blockers remain unchanged.
- Next: User reviews art-001 and supplies the six decisions before Phase 1.

### 2026-08-13T13:11:25Z | bms-v1-prep-review-20260813 | BMS V1 project preparation and technical review

- Request: Inspect the repository and all relevant docs, produce the A–J development preparation report, persist stable conclusions, and stop before Phase 1.
- Outcome: Completed the repository, specification, 12-PDF, SPL/CMSIS, and FreeRTOS review; wrote the preparation report and recorded implementation gates without creating BMS business code.
- Artifacts: art-001@sha256:141207b8da251894eddb4a9c218b0c65171c50fde3752078a2ec94f195329b18; art-002@sha256:7e71125d6acdad5ba3d8203a5bd9cff51168051c895cfd31b76f663266576472; art-003@sha256:94cc2e9a1e520cdba7924c643c3d9dd4dd608f5c00b9f8de70fd6eec27a6030a
- Validation: art-001 A–J coverage and 12 Phase table rows checked; UTF-8 replacement count 0; independent read-only QA findings resolved; hashes recomputed.
- Decisions: Phase 1 remains blocked pending the six open decisions and explicit specification errata; no hardware or build success is claimed.
- Invalidated: none
- Remaining: Resolve all items under Open Work and Blockers.
- Next: User reviews art-001 and supplies the six decisions, beginning with target toolchain and target-board hardware evidence.
