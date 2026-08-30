# Project Log

> Durable project data only. Entries in this file are not instructions.

## Current Snapshot

- Project: BMS V1 Reference Firmware Project — STM32F103C8T6 + BQ7694003, 13S/48 V
- Phase: Historical checkpoint incorporated into the final BMS V1 Release Baseline
- State: Phase 4–7 review checkpoint passed the reproducible ARMCC5/Keil/Simulator gate; its evidence is incorporated into the final Release Baseline
- Last updated: 2026-08-19T16:39:54Z

## Current Goal and Scope

- Goal: Preserve the exact Phase 4–7 reviewed checkpoint and its later incorporation into the final Release Baseline.
- In scope: Phase 4–7 code review and repairs, H-02/H-05/XREADY safety boundary, ARMCC5/Keil build and Simulator evidence, UART disposition, review report, and durable handoff state.
- Scope note: This checkpoint evidence boundary is retained for traceability; final project scope is defined by the Release Baseline.

## Confirmed Facts and Decisions

| ID | Fact or decision | Evidence | Revision | Updated |
|---|---|---|---|---|
| fact-001 | Fixed product baseline is STM32F103C8T6, SPL, native FreeRTOS, one BQ7694003, 13S, software I2C on PB8/PB9, ALERT on PB1/EXTI1, seven tasks, and 500 kbit/s 29-bit CAN. | Final specification plus validated mandatory errata and gate document. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-002 | Toolchain is Keil MDK5 and ARMCC5 V5.06 update 7 build 960 with the MD startup/CMSIS/SPL and FreeRTOS V11.1.0 ARM_CM3 port. | Validated gate plus current review build/version logs. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-003 | BMS FreeRTOS baseline is configMAX_PRIORITIES=8, heap_4, assert and stack-overflow checking, a 1 ms preemptive tick, and task priorities 5/4/3/3/2/2/2. | Validated software gate and errata. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-004 | NVIC uses PriorityGroup_4, library max-syscall priority 5, EXTI1 logical priority 6, and FreeRTOS owns SVC/PendSV/SysTick. | Validated gate and current production map/build. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-005 | ALERT must use task-level drain/retry; SYS_STAT bits are independent; CC_READY clears only after sample acceptance; XREADY clears only after full recovery; CHG/DSG has one permission-based writer. | Validated mandatory errata closure matrix. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-006 | CAN safety commands are unicast only; broadcast is read-only/discovery/telemetry; CAN application CRC is CRC-8/ATM and is separate from BQ CRC. | Validated gate and errata. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-007 | Application Flash is [0x08000000,0x0800F400); the final 3 KiB is reserved for SOC log and two parameter pages with final commit-marker semantics. | Validated mandatory errata and address review. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-008 | StateTask is system-health supervisor and sole application IWDG feeder; exact watchdog timing remains later implementation/hardware work. | Validated mandatory errata. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-009 | The checkpoint indexed PCB/BOM, NTC/OCV/Rsense calibration, MOS, balancing thermal, ALERT/WAKE, CAN, brownout, LSI timing, EMC/ESD, and safety-interface dimensions for controlled traceability. | Independently QA-validated gate verdict. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-010 | Generated documents belong in deliverables, firmware/project/tests in firmware, and docs remains read-only input/reference material. | Repository AGENTS.md and user decision. | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | 2026-08-13T13:40:42Z |
| fact-017 | BQ7694003 uses 7-bit address 0x08 and wire bytes 0x10/0x11; BQ CRC framing and adjacent-register atomic reads follow TI Rev.I. | Phase 3 actual-C trace tests and independent review. | sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018 | 2026-08-14T05:35:56Z |
| fact-018 | ADC gain is 365 plus the 5-bit trim in uV/LSB, ADCOFFSET is signed mV, and conversion uses signed wide arithmetic with nearest half-up mV rounding. | Phase 3 ARMCC5 tests and TI formulas. | sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018 | 2026-08-14T05:35:56Z |
| fact-019 | The Phase 3 accepted checkpoint is the `phase3-validated` tag at commit 83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a; later review evidence is incorporated into the final Release Baseline. | Git tag/commit inspection and phase reports. | git:83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a | 2026-08-19T16:39:54Z |
| fact-020 | Phase 3 exact 23-file source manifest is ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98 using UTF-8 path + NUL + lowercase file SHA-256 + LF. | Two independent exact-algorithm recomputations. | sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | 2026-08-14T05:43:19Z |
| fact-021 | The incoming DSH Phase 4–7 checkpoint was commit e2022e1735e331edb1bcca6ffaa589d28c2097cd; its reports are retained as evidence incorporated into the final Release Baseline. | Git history and report headers. | git:e2022e1735e331edb1bcca6ffaa589d28c2097cd | 2026-08-19T16:39:54Z |
| fact-022 | Reviewed firmware/evidence commit 1bba702f05eef7eb0f54041e0b3e67003d4959c2 passes the no-switch gate: 149 assertions, all Simulator failure counters zero, production 0 errors/0 warnings, Code=18576, RO=268, RW=200, ZI=10432. | build_phase7_review.ps1 exit 0; build/map/Simulator/verifier logs agree. | git:1bba702f05eef7eb0f54041e0b3e67003d4959c2 | 2026-08-19T16:39:54Z |
| fact-023 | H-05 lost-edge/startup-IRQ and H-02 CC queue/W1C defects are repaired with post-scheduler EXTI enable, level seeding, delayed pending retry, exact overflow diagnostics, and accepted-write/STOP outcome separation. | Production-C tests, active Thumb symbols, and verifier PASS. | git:1bba702f05eef7eb0f54041e0b3e67003d4959c2 | 2026-08-19T16:39:54Z |
| fact-024 | XREADY is fail-safe but not fully recovered in Phase 7: without an authoritative bounded recovery hook it remains active+latched, FET requests stay off, and XREADY is not W1C. | Review report and production-C XREADY tests. | sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969 | 2026-08-19T16:39:54Z |
| fact-025 | V1 requires debug UART1 on PA9/PA10 at 115200, but current firmware/project has no UART/USART implementation and no validated erratum supersedes the requirement. | Specification, Phase 2 report, project scan, and review report. | sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969 | 2026-08-19T16:39:54Z |

## Active Artifacts

| ID | Path | Revision | Status | Gate | Purpose | Evidence | Reuse guidance | Updated |
|---|---|---|---|---|---|---|---|---|
| art-004 | AGENTS.md | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | VALIDATED | ALLOW | Repository placement convention. | Explicit user decision and verified paths. | Apply unless the user changes it. | 2026-08-13T13:40:42Z |
| art-005 | deliverables/review/BMS_V1_规格勘误表.md | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | VALIDATED | ALLOW | Mandatory corrections overriding conflicting specification passages. | Independent final QA passed. | Always pair with the unified specification. | 2026-08-13T14:19:15Z |
| art-006 | deliverables/review/BMS_V1_Software_Implementation_Gate.md | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | VALIDATED | ALLOW | Software/hardware gate baseline. | Static review, exact checks, and independent QA passed. | Preserve evidence boundaries. | 2026-08-13T14:19:15Z |
| art-010 | deliverables/phase1/BMS_V1_Phase1_Report.md | sha256:c444a4fe0db8401ba22b9515aef22d1c1e17f5057a872140698bc269859ea0b9 | VALIDATED | ALLOW | Phase 1 completion record. | Report/build/map independently cross-checked. | Historical Phase 1 baseline only. | 2026-08-13T15:15:29Z |
| art-013 | firmware/Project/Keil/Build/BMS_V1_Phase2_build.log | sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79 | VALIDATED | ALLOW | Exact Phase 2 ARMCC5 build record. | ARMCC5 5.06u7 Clean/Rebuild 0 errors/0 warnings. | Historical Phase 2 evidence only. | 2026-08-14T05:10:27Z |
| art-016 | deliverables/phase2/BMS_V1_Phase2_Report.md | sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34 | VALIDATED | ALLOW | Phase 2 completion record. | Report hashes and independent QA passed. | Historical Phase 2 baseline only. | 2026-08-14T05:43:19Z |
| art-018 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294 | VALIDATED | ALLOW | Exact Phase 3 ARMCC5 project revision. | Phase 3 Clean/Rebuild, map, tests, and QA passed. | Historical Phase 3 project only; current path has a later revision. | 2026-08-14T05:35:56Z |
| art-019 | firmware/Project/Keil/Build/BMS_V1_Phase3_build.log | sha256:f2f523dbd30c7722cc640c0d127dba5382608737e041865748e4b2b37eb6e22c | VALIDATED | ALLOW | Exact Phase 3 build record. | ARMCC5 0/0 and size evidence. | Historical Phase 3 evidence only. | 2026-08-14T05:35:56Z |
| art-020 | firmware/Tests/Build/Phase3/phase3_simulator.log | sha256:95e6a301807a2e0b0f7bc4ebb101b9ab04bd8eb447b924c833f5f2e65e9e664c | VALIDATED | ALLOW | Phase 3 production-C Simulator evidence. | completed=1/failures=0 and map binding passed. | Evidence bound to the recorded Simulator trace and artifact identity. | 2026-08-14T05:35:56Z |
| art-021 | firmware/Tests/Build/Phase3/verify_phase3.log | sha256:43cec17a018d6203b009543d2636e70fc1cb6cf9406ee1463d3f51b81f6192fa | VALIDATED | ALLOW | Phase 3 verification gate. | All six groups and independent QA passed. | Re-run for changed inputs. | 2026-08-14T05:35:56Z |
| art-022 | deliverables/phase3/BMS_V1_Phase3_Report.md | sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018 | VALIDATED | ALLOW | Phase 3 completion record. | Report topics/hashes/evidence boundaries passed QA. | Accepted Phase 3 historical frontier. | 2026-08-14T05:43:19Z |
| art-023 | firmware | sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | VALIDATED | ALLOW | Exact Phase 3 source manifest. | True NUL/LF algorithm independently reproduced. | Reuse only if all 23 files reproduce the revision. | 2026-08-14T05:43:19Z |
| art-024 | firmware | git:1bba702f05eef7eb0f54041e0b3e67003d4959c2 | USABLE | RECHECK | Phase 4–7 repaired code, tests, runner, and checked-in checkpoint evidence. | Full no-switch review gate exit 0; three independent read-only audits found no remaining Critical/High blocker. | Retain exact commit and evidence identity within the final verification chain. | 2026-08-19T16:39:54Z |
| art-025 | deliverables/review/BMS_V1_Codex_Phase7_Review.md | sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969 | USABLE | RECHECK | Phase 4–7 findings, repairs, evidence boundaries, UART disposition, and handoff. | Cross-checked against final build/map/Simulator/verifier and three focused audits. | Checkpoint retained and incorporated into the final Release Baseline. | 2026-08-19T16:39:54Z |
| art-026 | firmware/Project/Keil/Build/BMS_V1_Codex_Phase7_build.log | sha256:bace3439403de4ad969b76989636aa67f6f0ebc6d5fd1028b82b7c54d0892dcb | USABLE | RECHECK | Final production ARMCC5 Clean/Rebuild evidence for art-024. | 32 units, ARMCC5 5.06u7, 0 errors/0 warnings, final sizes. | Exact recorded revision; rebuild after any input change. | 2026-08-19T16:39:54Z |
| art-027 | firmware/Tests/Build/Phase7Review/phase7_review_simulator.log | sha256:9b6c5065d94bba0262d5403321361b2e00c5e08de0b26eb4b05c923c0025447a | USABLE | RECHECK | Phase 4/6/5/7 review image execution evidence. | All completion probes set and all failure counters zero. | Evidence bound to the recorded production-C Simulator method. | 2026-08-19T16:39:54Z |
| art-028 | firmware/Tests/Build/Phase7Review/verify_phase7_review.log | sha256:f456bca812a6d7cf632d2a63f8c9321403222470d00d2a37b6bb236f4219e735 | USABLE | RECHECK | Fail-closed source/project/map/build/Simulator/freshness gate. | 149 PASS assertions, 2 INFO lines, final PASS. | Re-run no-switch runner after any declared input change. | 2026-08-19T16:39:54Z |
| art-029 | firmware/Tests/build_phase7_review.ps1 | sha256:1a49d30831f7efb3d45d069d4f458e95dde10b67af17a6392617af4eaf6ede45 | USABLE | RECHECK | Reproducible Windows/Keil/ARMCC5 review runner. | Used for exact art-026 through art-028 generation; paths/options/freshness audited. | Requires licensed uVision 5.38, ARMCC5 5.06u7, and Python 3. | 2026-08-19T16:39:54Z |

## Invalidated Artifact Tombstones

| ID | Path | Revision | Status | Gate | Reason | Replacement | Evidence | Updated |
|---|---|---|---|---|---|---|---|---|
| art-007 | firmware | sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d | SUPERSEDED | BLOCK | Phase 1 source manifest was extended in Phase 2. | art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942 | Historical Phase 1 record retained in art-010. | 2026-08-14T05:10:27Z |
| art-008 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a | SUPERSEDED | BLOCK | Phase 1 project was extended in Phase 2. | art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99 | Phase 2 project/build were validated. | 2026-08-14T05:10:27Z |
| art-009 | firmware/Project/Keil/Build/BMS_V1_build.log | sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26 | SUPERSEDED | BLOCK | Phase 1 log does not prove later sources. | art-013@sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79 | Phase 2 rebuild was validated. | 2026-08-14T05:10:27Z |
| art-011 | firmware | sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942 | SUPERSEDED | BLOCK | Phase 2 source manifest was extended in Phase 3. | art-023@sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | Phase 3 exact manifest validated. | 2026-08-14T05:43:19Z |
| art-012 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99 | SUPERSEDED | BLOCK | Phase 2 project was extended in Phase 3. | art-018@sha256:7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294 | Phase 3 project/build validated. | 2026-08-14T05:35:56Z |
| art-017 | firmware | sha256:11f7e139ec06c2a18ff3c379559ec608682b4513a45e3edde191a7717adeb298 | SUPERSEDED | BLOCK | Manifest used literal escape bytes rather than true NUL/LF. | art-023@sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | Independent recomputation exposed and corrected the mismatch. | 2026-08-14T05:43:19Z |

## Checkpoint Closure

- Review status: Checkpoint evidence retained and incorporated into the final Release Baseline.
- Debug UART integration is incorporated into the final diagnostic evidence chain.
- The phaseful authoritative XREADY recovery implementation is incorporated into the final Release Baseline.
- Under a prolonged definitely rejected CC W1C, old/new continuously high CC_READY states cannot be distinguished and a conversion may coalesce; exact zero-loss is not claimed.
- Production-C Simulator, scheduler stress, and hardware-interface observations retain distinct evidence identities in the final verification chain.
- Vendor pre-main HSE/PLL wait behavior remains unbounded if the vendor startup never reaches main.
- Continuation: The subsequent phase evidence is incorporated into the final Release Baseline.

## Recent Task History

### 2026-08-19T16:39:54Z | codex-phase7-review-20260819 | Review and repair Phase 4–7 checkpoint

- Request: Take over the repository at the DSH Phase 7 frontier, review Phase 4–7 against the specification, repair confirmed safety defects without implementing Phase 8+, execute the real toolchain, report evidence/UART status, and prepare a pushed review branch.
- Outcome: Repaired BQ thresholds/offset/composer/FET contracts, write-commit semantics, ALERT lost-edge/startup IRQ handling, CC queue diagnostics/W1C ordering, conservative XREADY handling, and fatal paths; added a reproducible review gate and final report.
- Artifacts: art-024@git:1bba702f05eef7eb0f54041e0b3e67003d4959c2; art-025@sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969; art-026@sha256:bace3439403de4ad969b76989636aa67f6f0ebc6d5fd1028b82b7c54d0892dcb; art-027@sha256:9b6c5065d94bba0262d5403321361b2e00c5e08de0b26eb4b05c923c0025447a; art-028@sha256:f456bca812a6d7cf632d2a63f8c9321403222470d00d2a37b6bb236f4219e735; art-029@sha256:1a49d30831f7efb3d45d069d4f458e95dde10b67af17a6392617af4eaf6ede45
- Validation: No-switch runner exit 0; 149 verifier assertions PASS; all Simulator counters zero; production ARMCC5 5.06u7 Clean/Rebuild 0 errors/0 warnings; final ROM 19044 and link-time RAM 10632 bytes; three focused read-only audits found no remaining Critical/High code blocker.
- Decisions: Preserve the Phase 1–7 evidence chain; later UART and phaseful XREADY recovery closure are incorporated into the final Release Baseline.
- Invalidated: none.
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
- Next: Push `codex/review-phase7`, then seek independent review/acceptance or an explicit Phase 8 continuation command.

### 2026-08-14T05:43:19Z | phase3-manifest-correction-20260814 | Correct Phase 3 source manifest serialization

- Request: Resolve the final QA mismatch between the recorded Phase 3 firmware manifest algorithm and its stored revision without changing implementation evidence or rewriting committed history.
- Outcome: Recomputed the 23-file manifest with true NUL and LF bytes, replaced the incorrect revision with art-023, preserved art-017 as a blocking tombstone, and left firmware/build/tests/report unchanged.
- Artifacts: art-023@sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b
- Validation: Two exact Python recomputations using byte values 0 and 10 produced the same digest; all 23 file hashes, art-018 through art-022, verify_phase3, build sizes, and no-Phase4 scan remain unchanged and PASS.
- Decisions: Manifest serialization is now explicitly path UTF-8 + NUL + lowercase hexadecimal file SHA-256 + LF; literal escape characters are invalid for this revision definition.
- Invalidated: art-017@sha256:11f7e139ec06c2a18ff3c379559ec608682b4513a45e3edde191a7717adeb298
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
- Next: Wait for the user's explicit Phase 4 instruction.

### 2026-08-14T05:35:56Z | phase3-gate-20260814 | Complete Phase 3 BQ transport, calibration, and hard gate

- Request: Execute Phase 2 then Phase 3 in strict order, commit the Phase 2 checkpoint before Phase 3, implement only authorized BQ transport/calibration/basic conversion, validate with real ARMCC5 and actual-C mock execution, report both gates, and establish the recorded entry checkpoint.
- Outcome: Consumed the exact Phase 2 checkpoint; implemented TI-aligned BQ7694003 register transport, CRC framing, deferred ACK/NACK decisions, atomic block reads, calibration decode, and pure cell conversion; Phase 3 Hard Gate passed without entering Phase 4.
- Artifacts: art-017@sha256:11f7e139ec06c2a18ff3c379559ec608682b4513a45e3edde191a7717adeb298; art-018@sha256:7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294; art-019@sha256:f2f523dbd30c7722cc640c0d127dba5382608737e041865748e4b2b37eb6e22c; art-020@sha256:95e6a301807a2e0b0f7bc4ebb101b9ab04bd8eb447b924c833f5f2e65e9e664c; art-021@sha256:43cec17a018d6203b009543d2636e70fc1cb6cf9406ee1463d3f51b81f6192fa; art-022@sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018
- Validation: ARMCC5 actual-C test map binds current production BQ/CRC objects and mock SoftI2C; Simulator completed=1/failures=0; verify_phase3.py exit 0; ARMCC5 V5.06u7 build 960 Clean+Rebuild 0 errors/0 warnings with Code=3164, RO=268, RW=32, ZI=1896; all report hashes and 22 topics independently QA-validated.
- Decisions: Read mismatch always NACKs then attempts STOP and returns CRC_MISMATCH even if response/cleanup reports failure; output/calibration commit only after complete success; no default CC_CFG write, probe, wake, sampling, protection, or Phase 4 behavior.
- Invalidated: art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942; art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
- Next: Wait for the user's explicit Phase 4 instruction.

### 2026-08-14T05:10:27Z | phase2-gate-20260814 | Complete Phase 2 BSP, software-I2C, CRC, and hard gate

- Request: Execute Phase 2 first, including MCU BSP, TIM3, bounded software-I2C, BQ CRC, tests, ARMCC5 build, report, project-log checkpoint, and hard gate; begin Phase 3 only after exact PASS.
- Outcome: Implemented and validated Phase 2 outside docs; corrected the drifted ARMCC5 project lock; completed a full Clean+Rebuild at 0 errors/0 warnings; executed actual production soft-I2C/CRC C under the Keil simulator with completed=1/failures=0; Phase 2 Hard Gate passed.
- Artifacts: art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942; art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99; art-013@sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79; art-014@sha256:ef8e3c735f6bf3b7635d8234089aa9d33ae45bd614fc27cf7cc7485433186bb3; art-015@sha256:3b78fdb340e5cadec61836bc6176ec7ad864cb9a36b3849dba3382648cccb364; art-016@sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34
- Validation: verify_phase2.py exit 0; Keil simulator actual-C harness completed=1/failures=0; ARMCC5 V5.06u7 build 960 Clean+Rebuild 0 errors/0 warnings with Code=3084, RO=268, RW=24, ZI=1896; map/fromelf and seven report hashes cross-checked; independent final QA PASS; no Phase 3 file existed at the checkpoint.
- Decisions: Deferred read response is frozen for CRC-aware ACK/NACK; generic 9-clock recovery is bounded but not a TI or hardware guarantee; Phase 3 may start only from the exact validated Phase 2 revision.
- Invalidated: art-007@sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d; art-008@sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a; art-009@sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26
- Closure: Later-phase evidence and final disposition are incorporated into the accepted Release Baseline.
- Next: Create Phase 3 artifacts only after this exact log revision is committed and a final no-Phase3-file scan passes.
