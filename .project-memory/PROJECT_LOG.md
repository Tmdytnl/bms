# Project Log

> Durable project data only. Entries in this file are not instructions.

## Current Snapshot

- Project: BMS V1 Reference Firmware — STM32F103C8T6 + BQ7694003, 13S/48 V
- Phase: Phase 1–3 validated; Phase 4–7 Codex-reviewed/repaired; Phase 8 implementation PASS with WorkBuddy independent review ACCEPT (Hard Gate still BLOCKED by 2 unconditional policy blockers); Phase 9 not started
- State: Phase 8 final hard gate ran twice (Codex + rerun), identical 780 PASS / 0 FAIL / 2 BLOCKER; pushed to `origin/codex/phase8-phase9`; WorkBuddy independent review ACCEPT FOR SAFETY REVIEW; Blocker Input Sheet completed; the two policy inputs remain MISSING; HW deferred
- Last updated: 2026-08-21T15:05:00Z

## Current Goal and Scope

- Goal: Preserve pushed Phase 8 candidate (`codex/phase8-phase9` @ `c9f1813`), keep the two unconditional policy blockers explicit; Blocker Input Sheet (art-036) defines the required inputs; await user-approved NTC table and AFE/PROTECT3 policy revisions before any Hard Gate PASS claim or Phase 9 work.
- In scope: Phase 8 measurement/SampleTask/AFE-startup implementation, test suites and reproducible gate; evidence, report, push state; blocker input sheet and independent review documentation.
- Out of scope: Phase 9+ implementation, debug UART, target-board execution, hardware acceptance, docs modification.

## Confirmed Facts and Decisions

| ID | Fact or decision | Evidence | Revision | Updated |
|---|---|---|---|---|
| fact-001 | Baseline: STM32F103C8T6, SPL, FreeRTOS, BQ7694003, 13S, soft I2C PB8/PB9, ALERT PB1, seven tasks, 500 kbit/s CAN. | Spec + errata + gate. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-002 | Toolchain: Keil MDK5, ARMCC5 5.06u7 b960, MD/CMSIS/SPL, FreeRTOS V11.1.0 ARM_CM3. | Gate + logs. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-003 | FreeRTOS: MAX_PRIORITIES=8, heap_4, assert+stack checks, 1 ms tick, priorities 5/4/3/3/2/2/2. | Gate + errata. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-004 | NVIC Group_4, syscall priority 5, EXTI1 priority 6; FreeRTOS owns SVC/PendSV/SysTick. | Gate + map. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-005 | ALERT: task drain/retry; SYS_STAT bits independent; CC_READY clears after sample accept; XREADY after recovery; CHG/DSG one writer. | Errata matrix. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-006 | CAN commands unicast only; broadcast read-only/telemetry; CAN CRC is CRC-8/ATM, separate from BQ CRC. | Gate + errata. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-007 | App Flash [0x08000000,0x0800F400); final 3 KiB for SOC log + two parameter pages. | Errata + review. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-008 | StateTask is health supervisor and sole IWDG feeder; timing remains hardware work. | Errata. | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | 2026-08-13T14:19:15Z |
| fact-009 | SW/HW evidence separate; PCB/BOM, calibration, power, thermal, waveforms, buses, timing, EMC/ESD remain HW-gate. | Impl. gate. | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | 2026-08-13T14:19:15Z |
| fact-010 | Generated docs in deliverables, code/tests in firmware; docs is read-only. | AGENTS.md + decision. | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | 2026-08-13T13:40:42Z |
| fact-017 | BQ7694003: addr 0x08, wire 0x10/0x11; BQ CRC framing + atomic block reads per TI Rev.I. | P3 trace tests. | sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018 | 2026-08-14T05:35:56Z |
| fact-018 | ADC gain 365 + trim in uV/LSB; ADCOFFSET signed mV; wide arithmetic, half-up rounding. | P3 tests + TI. | sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018 | 2026-08-14T05:35:56Z |
| fact-019 | Accepted frontier: `phase3-validated` -> 83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a; no Phase 4-7 revision independently VALIDATED. | Git. | git:83bd3be9e5c2ee20e8c9ef7b1ac4bf88847c8d1a | 2026-08-19T16:39:54Z |
| fact-020 | Phase 3 exact 23-file manifest ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98 (UTF-8 + NUL + SHA-256 + LF). | Two recomputations. | sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | 2026-08-14T05:43:19Z |
| fact-021 | Incoming DSH frontier was e2022e1735e331edb1bcca6ffaa589d28c2097cd; Phase 4-7 reports candidates. | Git + headers. | git:e2022e1735e331edb1bcca6ffaa589d28c2097cd | 2026-08-19T16:39:54Z |
| fact-022 | 1bba702f05eef7eb0f54041e0b3e67003d4959c2 passes no-switch gate: 149 assertions, Simulator zero, production 0/0, Code=18576 RO=268 RW=200 ZI=10432. | Runner exit 0; build/map/Sim/verifier. | git:1bba702f05eef7eb0f54041e0b3e67003d4959c2 | 2026-08-19T16:39:54Z |
| fact-023 | H-05/H-02 repaired: post-scheduler EXTI enable, level seeding, delayed retry, overflow diagnostics, accepted-write/STOP separation. | Tests + verifier PASS. | git:1bba702f05eef7eb0f54041e0b3e67003d4959c2 | 2026-08-19T16:39:54Z |
| fact-024 | XREADY fail-safe but not recovered in Phase 7: without bounded hook stays active+latched, FETs off, not W1C. | Review + tests. | sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969 | 2026-08-19T16:39:54Z |
| fact-025 | V1 requires UART1 PA9/PA10 @115200; no UART implementation, no validated erratum. | Spec + P2 report. | sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969 | 2026-08-19T16:39:54Z |
| fact-026 | Phase 8 gate ran twice identically: production 0/0 (Code=24324 RO=284 RW=268 ZI=10588), six images, Simulator zero failures, verifier 780 PASS / 0 FAIL / 2 BLOCKER; TEST/EVIDENCE PASS, GATE BLOCKED. | Codex 2026-08-20T17:33:30Z + rerun 2026-08-21T13:12:02Z. | git:c9f1813096dcea23d7e9495a00c07e6414595c44 | 2026-08-21T13:30:00Z |
| fact-027 | Two unconditional blockers: no user-approved immutable NTC table revision and no user-approved immutable AFE startup/PROTECT3 policy revision; gate revision phase8-hard-gate-redteam-r3-20260821 cannot be silently satisfied. | verify_phase8.py + log. | git:c9f1813096dcea23d7e9495a00c07e6414595c44 | 2026-08-21T13:30:00Z |
| fact-028 | Phase 8 candidate pushed as `origin/codex/phase8-phase9` @ `c9f1813`; history preserved. | branch + ls-remote. | git:c9f1813096dcea23d7e9495a00c07e6414595c44 | 2026-08-21T13:30:00Z |
| fact-029 | Phase 8 software implementation PASS per WorkBuddy independent review: ACCEPT FOR SAFETY REVIEW over `origin/codex/review-phase7..codex/phase8-phase9` (4cb25b6..08bf194); no Critical/High/Medium findings; one Low (L-01: Phase 8 report push/HEAD history state and Task_Sample priority text stale). | Review report + WorkBuddy memory. | sha256:db17cf13732bd88ed5028214a3184d9b0eaa97851f22ec5f5c3b6a2954f90fea | 2026-08-21T15:05:00Z |
| fact-030 | Blocker Input Sheet completed: reverse requirement analysis of both blockers; blocker definitions sound but need refinement (NTC table must bind part/TS1 circuit/coverage/approval revision; AFE policy must cover full startup registers incl. SCD in PROTECT1, OCD in PROTECT2, RSNS, calibration handoff, XREADY/FET policy); no third blocker; minimum unblock input sets and future gate binding rules defined. | Sheet + WorkBuddy memory. | sha256:9b315f333090a49069cb1144d1757621a618b8f2f019490534a169e94ca5b0db | 2026-08-21T15:05:00Z |
| fact-031 | Phase 8 Hard Gate remains BLOCKED(2): the two unconditional inputs (approved immutable NTC table revision; approved immutable AFE startup/PROTECT3 policy revision) remain MISSING; independent ACCEPT does not lift the verifier's unconditional `block()`. | Review verdict + verifier. | git:08bf1944d12ea57f439a8710718e7057ac7fe5e4 | 2026-08-21T15:05:00Z |

## Active Artifacts

| ID | Path | Revision | Status | Gate | Purpose | Evidence | Reuse guidance | Updated |
|---|---|---|---|---|---|---|---|---|
| art-004 | AGENTS.md | sha256:181b2a412937ef390d1ce177a95ffde63b0006953c2364ddcf6232980c90b366 | VALIDATED | ALLOW | Placement convention. | User decision + paths. | Apply unless changed. | 2026-08-13T13:40:42Z |
| art-005 | deliverables/review/BMS_V1_规格勘误表.md | sha256:65ec79225425fd23cd74b7e988276b76d2d876d99101f5606295aca609d85c1e | VALIDATED | ALLOW | Corrections overriding spec passages. | QA passed. | Pair with spec. | 2026-08-13T14:19:15Z |
| art-006 | deliverables/review/BMS_V1_Software_Implementation_Gate.md | sha256:635b16fe8401dccef01c394b9d7d78a612c5e7f1a957b7e568b0936062a17867 | VALIDATED | ALLOW | SW/HW gate baseline. | Static review, checks, QA. | Preserve evidence. | 2026-08-13T14:19:15Z |
| art-010 | deliverables/phase1/BMS_V1_Phase1_Report.md | sha256:c444a4fe0db8401ba22b9515aef22d1c1e17f5057a872140698bc269859ea0b9 | VALIDATED | ALLOW | Phase 1 record. | Report/build/map checked. | Hist. Phase 1 baseline. | 2026-08-13T15:15:29Z |
| art-013 | firmware/Project/Keil/Build/BMS_V1_Phase2_build.log | sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79 | VALIDATED | ALLOW | Phase 2 build record. | Clean/Rebuild 0/0. | Hist. Phase 2 evidence. | 2026-08-14T05:10:27Z |
| art-016 | deliverables/phase2/BMS_V1_Phase2_Report.md | sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34 | VALIDATED | ALLOW | Phase 2 record. | Hashes + QA. | Hist. Phase 2 baseline. | 2026-08-14T05:43:19Z |
| art-018 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294 | VALIDATED | ALLOW | Phase 3 project revision. | Clean/Rebuild, map, tests, QA. | Hist. Phase 3 project; path later. | 2026-08-14T05:35:56Z |
| art-019 | firmware/Project/Keil/Build/BMS_V1_Phase3_build.log | sha256:f2f523dbd30c7722cc640c0d127dba5382608737e041865748e4b2b37eb6e22c | VALIDATED | ALLOW | Phase 3 build record. | 0/0 + sizes. | Hist. Phase 3 evidence. | 2026-08-14T05:35:56Z |
| art-020 | firmware/Tests/Build/Phase3/phase3_simulator.log | sha256:95e6a301807a2e0b0f7bc4ebb101b9ab04bd8eb447b924c833f5f2e65e9e664c | VALIDATED | ALLOW | Phase 3 Simulator evidence. | completed=1/failures=0 + map. | SW/Sim only. | 2026-08-14T05:35:56Z |
| art-021 | firmware/Tests/Build/Phase3/verify_phase3.log | sha256:43cec17a018d6203b009543d2636e70fc1cb6cf9406ee1463d3f51b81f6192fa | VALIDATED | ALLOW | Phase 3 verification gate. | Six groups + QA. | Re-run on change. | 2026-08-14T05:35:56Z |
| art-022 | deliverables/phase3/BMS_V1_Phase3_Report.md | sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018 | VALIDATED | ALLOW | Phase 3 record. | Topics/hashes/evidence QA. | Phase 3 frontier. | 2026-08-14T05:43:19Z |
| art-023 | firmware | sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | VALIDATED | ALLOW | Phase 3 source manifest. | NUL/LF algorithm reproduced. | Reuse if 23 files match. | 2026-08-14T05:43:19Z |
| art-024 | firmware | git:1bba702f05eef7eb0f54041e0b3e67003d4959c2 | USABLE | RECHECK | Phase 4-7 code, tests, runner, evidence. | Gate exit 0; audits clean. | Recheck; not accepted. | 2026-08-19T16:39:54Z |
| art-025 | deliverables/review/BMS_V1_Codex_Phase7_Review.md | sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969 | USABLE | RECHECK | Phase 4-7 findings, repairs, evidence, UART. | Checked + audits. | Review before acceptance. | 2026-08-19T16:39:54Z |
| art-026 | firmware/Project/Keil/Build/BMS_V1_Codex_Phase7_build.log | sha256:bace3439403de4ad969b76989636aa67f6f0ebc6d5fd1028b82b7c54d0892dcb | USABLE | RECHECK | Production ARMCC5 Clean/Rebuild evidence. | 32 units, 0/0, sizes. | Exact candidate; rebuild on change. | 2026-08-19T16:39:54Z |
| art-027 | firmware/Tests/Build/Phase7Review/phase7_review_simulator.log | sha256:9b6c5065d94bba0262d5403321361b2e00c5e08de0b26eb4b05c923c0025447a | USABLE | RECHECK | Phase 4/6/5/7 review image evidence. | Probes set, counters zero. | SW/Sim only. | 2026-08-19T16:39:54Z |
| art-028 | firmware/Tests/Build/Phase7Review/verify_phase7_review.log | sha256:f456bca812a6d7cf632d2a63f8c9321403222470d00d2a37b6bb236f4219e735 | USABLE | RECHECK | Fail-closed source/project/map/build gate. | 149 PASS, 2 INFO. | Re-run on change. | 2026-08-19T16:39:54Z |
| art-029 | firmware/Tests/build_phase7_review.ps1 | sha256:1a49d30831f7efb3d45d069d4f458e95dde10b67af17a6392617af4eaf6ede45 | USABLE | RECHECK | Reproducible Windows/Keil/ARMCC5 runner. | For art-026..028. | uVision 5.38, ARMCC5 5.06u7. | 2026-08-19T16:39:54Z |
| art-030 | firmware | git:c9f1813096dcea23d7e9495a00c07e6414595c44 | USABLE | RECHECK | Phase 8 implementation + tests + harness + report. | Twice: 780 PASS / 0 FAIL / 2 BLOCKER; 0/0; pushed. | Recheck; BLOCKED. | 2026-08-21T13:30:00Z |
| art-031 | deliverables/phase8/BMS_V1_Phase8_Report.md | sha256:8838a0d4dce62754299a5994ef2ea0778daaa590a7031a7c2186c9d8cdb8eda4 | USABLE | RECHECK | Phase 8 record with verdict and blockers. | Matches verify log. | Candidate; recheck later. | 2026-08-21T13:30:00Z |
| art-032 | firmware/Tests/Build/Phase8/verify_phase8.log | sha256:b1cbaceed895ba6d59928bf61c06575ede6244cdb73a81f36be1b641ee9fc922 | USABLE | RECHECK | Phase 8 verifier evidence. | 780 PASS / 0 FAIL / 2 BLOCKER (rerun). | Exact candidate; rebuild on change. | 2026-08-21T13:30:00Z |
| art-033 | firmware/Tests/Build/Phase8/phase8_build.log | sha256:bb515952c29a255ac3d7ae816a1e9a5b8db13c2d91f08fb260968745d297aea4 | USABLE | RECHECK | Phase 8 test-image build + manifest. | 6 PASS, 144 inputs. | Exact candidate; rebuild on change. | 2026-08-21T13:30:00Z |
| art-034 | firmware/Tests/Build/Phase8/phase8_simulator.log | sha256:58b7ce863a9fe685f55989d73b4bcbdefabb8293b2b2d3ec57e48452d517e16d | USABLE | RECHECK | Phase 8 Simulator evidence. | Six images completed=1/failures=0. | SW/Sim only. | 2026-08-21T13:30:00Z |
| art-035 | firmware/Project/Keil/Build/BMS_V1_Phase8_build.log | sha256:a627324b9d47ace36e566cd2f37f31a01dba503af0f8b22c688a53e9e0a2a1f9 | USABLE | RECHECK | Phase 8 production ARMCC5 Clean/Rebuild. | 0/0; Code=24324 RO=284 RW=268 ZI=10588. | Exact candidate; rebuild on change. | 2026-08-21T13:30:00Z |
| art-036 | deliverables/phase8/BMS_V1_Phase8_Blocker_Input_Sheet.md | sha256:9b315f333090a49069cb1144d1757621a618b8f2f019490534a169e94ca5b0db | VALIDATED | ALLOW | Blocker 1/2 required-input analysis, minimum unblock sets, future gate binding rules. | User-directed incorporation; content cross-checked vs production C + verifier. | Reference for future gate binding; not an approved policy artifact. | 2026-08-21T15:05:00Z |
| art-037 | deliverables/review/BMS_V1_Phase8_Independent_Review.md | sha256:db17cf13732bd88ed5028214a3184d9b0eaa97851f22ec5f5c3b6a2954f90fea | VALIDATED | ALLOW | WorkBuddy independent review record (ACCEPT FOR SAFETY REVIEW). | WorkBuddy review + memory. | ACCEPT is not Hard Gate PASS; gate stays BLOCKED(2). | 2026-08-21T15:05:00Z |

## Invalidated Artifact Tombstones

| ID | Path | Revision | Status | Gate | Reason | Replacement | Evidence | Updated |
|---|---|---|---|---|---|---|---|---|
| art-007 | firmware | sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d | SUPERSEDED | BLOCK | P1 manifest extended in P2. | art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942 | Record in art-010. | 2026-08-14T05:10:27Z |
| art-008 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a | SUPERSEDED | BLOCK | P1 project extended in P2. | art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99 | P2 project/build validated. | 2026-08-14T05:10:27Z |
| art-009 | firmware/Project/Keil/Build/BMS_V1_build.log | sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26 | SUPERSEDED | BLOCK | P1 log does not prove later sources. | art-013@sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79 | P2 rebuild validated. | 2026-08-14T05:10:27Z |
| art-011 | firmware | sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942 | SUPERSEDED | BLOCK | P2 manifest extended in P3. | art-023@sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | P3 manifest validated. | 2026-08-14T05:43:19Z |
| art-012 | firmware/Project/Keil/BMS_V1.uvprojx | sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99 | SUPERSEDED | BLOCK | P2 project extended in P3. | art-018@sha256:7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294 | P3 project/build validated. | 2026-08-14T05:35:56Z |
| art-017 | firmware | sha256:11f7e139ec06c2a18ff3c379559ec608682b4513a45e3edde191a7717adeb298 | SUPERSEDED | BLOCK | Manifest used escape bytes, not NUL/LF. | art-023@sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b | Recomputation exposed mismatch. | 2026-08-14T05:43:19Z |

## Open Work and Blockers

- Phase 4–7 remains `USABLE / RECHECK`; Phase 8 implementation independently reviewed ACCEPT FOR SAFETY REVIEW, but Hard Gate remains `BLOCKED(2)`; final acceptance awaits Codex Sol High safety review and gate PASS.
- Debug UART is V1-required but missing; accept UART1 PA9/PA10 @115200 before V1 completion.
- XREADY recovery state machine in `bms_afe_startup.c`, not connected in production until AFE startup/PROTECT3 policy approved; production fail-safe.
- Phase 8 Hard Gate BLOCKED by two unconditional inputs: user-approved immutable NTC table revision + AFE startup/PROTECT3 policy revision; Blocker Input Sheet (art-036) defines the required inputs; Phase 9 waits for both approvals.
- Phase 8 report L-01 (independent review): push/HEAD history state and Task_Sample priority text stale; doc-hygiene fix pending, no gate rerun required.
- Under a prolonged rejected CC W1C, old/new high CC_READY states cannot be distinguished; zero-loss not claimed.
- Hardware validation and real preemptive/ISR/I2C-contention/stack stress remain required; Simulator is not board evidence.
- Vendor pre-main HSE/PLL wait remains unbounded if startup never reaches main.

## Recent Task History

### 2026-08-21T15:05:00Z | phase8-blocker-input-20260821 | Incorporate Phase 8 blocker input sheet and independent review into project docs

- Request: Place `BMS_V1_Phase8_Blocker_Input_Sheet.md` under deliverables/phase8/; record in PROJECT_LOG: Phase 8 software implementation PASS, WorkBuddy independent review ACCEPT, Blocker Input Sheet completed, Hard Gate remains BLOCKED(2); no firmware/tests/verifier/Phase9 changes; no gate rerun; commit.
- Outcome: Blocker input sheet confirmed at deliverables/phase8/ (requirements analysis only; no code change); WorkBuddy ACCEPT FOR SAFETY REVIEW recorded; Hard Gate stays BLOCKED(2); log updated and committed.
- Artifacts: art-036@sha256:9b315f333090a49069cb1144d1757621a618b8f2f019490534a169e94ca5b0db; art-037@sha256:db17cf13732bd88ed5028214a3184d9b0eaa97851f22ec5f5c3b6a2954f90fea
- Validation: Sheet/review content cross-checked against production C references and verifier blocker lines; SHA-256 computed; no firmware/tests/verifier modification; gate not rerun; legacy project-memory archive helper conflict, manual update applied
- Decisions: Blocker 1 needs part-bound immutable NTC table with TS1 circuit/coverage/approval binding; Blocker 2 needs full AFE startup register policy (incl. SCD in PROTECT1, OCD in PROTECT2, RSNS, calibration handoff, XREADY/FET policy); no third blocker; both inputs remain MISSING.
- Invalidated: none.
- Remaining: Approved NTC table and AFE startup/PROTECT3 policy artifacts; Phase 8 report L-01 doc hygiene; Codex Sol High safety review; hardware validation.
- Next: Await the two approved policy inputs before any Hard Gate PASS claim or Phase 9 start.

### 2026-08-21T13:30:00Z | phase8-handoff-20260821 | Complete Phase 8 handoff after Codex interruption

- Request: Take over repo after Codex quota exhaustion; preserve uncommitted Phase 7 fix and Phase 8 work; verify final gate, rerun if needed; commit/push codex/phase8-phase9; generate Phase 8 report.
- Outcome: Recovered worktree; proved Codex gate completed (17:33:30Z); committed impl/tests/evidence; ran independent gate (13:12:02Z) reproducing maps/simulator/verifier; generated Report; pushed @ c9f1813; no overwrite; efb2283 preserved.
- Artifacts: art-030@git:c9f1813096dcea23d7e9495a00c07e6414595c44; art-031..art-035 in Active Artifacts and Report.
- Validation: Production 0/0 (Code=24324); six images; Simulator 0 failures; verifier 780 PASS/0 FAIL/2 BLOCKER twice; stack 464 B/768 B; uvoptx restored; details in Report.
- Decisions: Phase 1-3 frontier kept; Phase 4-8 USABLE/RECHECK; verdict TEST/EVIDENCE PASS + HARD GATE BLOCKED (2); Phase 9 NOT STARTED; blockers cleared only by user-approved immutable revisions.
- Invalidated: none.
- Remaining: User approval of NTC table and AFE startup/PROTECT3 policy revisions; bind in future gate revision; UART; hardware validation.
- Next: Await the two approved policy inputs before any Hard Gate PASS claim or Phase 9 start.

### 2026-08-19T16:39:54Z | codex-phase7-review-20260819 | Review and repair Phase 4–7 candidate

- Request: Take over the repository at the DSH Phase 7 frontier, review Phase 4–7 against the specification, repair confirmed safety defects without implementing Phase 8+, execute the real toolchain, report evidence/UART status, and prepare a pushed review branch.
- Outcome: Repaired BQ thresholds/offset/composer/FET contracts, write-commit semantics, ALERT lost-edge/startup IRQ handling, CC queue diagnostics/W1C ordering, conservative XREADY handling, and fatal paths; added a reproducible review gate and final report.
- Artifacts: art-024@git:1bba702f05eef7eb0f54041e0b3e67003d4959c2; art-025@sha256:4790d02ad60f912fc553da9e04e67b8f659e0105fcbcc5975143748bb8381969; art-026@sha256:bace3439403de4ad969b76989636aa67f6f0ebc6d5fd1028b82b7c54d0892dcb; art-027@sha256:9b6c5065d94bba0262d5403321361b2e00c5e08de0b26eb4b05c923c0025447a; art-028@sha256:f456bca812a6d7cf632d2a63f8c9321403222470d00d2a37b6bb236f4219e735; art-029@sha256:1a49d30831f7efb3d45d069d4f458e95dde10b67af17a6392617af4eaf6ede45
- Validation: No-switch runner exit 0; 149 verifier assertions PASS; all Simulator counters zero; production ARMCC5 5.06u7 Clean/Rebuild 0 errors/0 warnings; final ROM 19044 and link-time RAM 10632 bytes; three focused read-only audits found no remaining Critical/High code blocker.
- Decisions: Preserve Phase 1–3 as the accepted validation frontier; classify this Phase 4–7 revision as `USABLE / RECHECK`; UART remains required/missing; XREADY full recovery and hardware evidence remain deferred.
- Invalidated: none.
- Remaining: Independent acceptance, UART, authoritative XREADY recovery, prolonged-bus-fault/stress validation, target hardware validation, and explicit authorization before Phase 8.
- Next: Push `codex/review-phase7`, then seek independent review/acceptance or an explicit Phase 8 continuation command.

### 2026-08-14T05:43:19Z | phase3-manifest-correction-20260814 | Correct Phase 3 source manifest serialization

- Request: Resolve the final QA mismatch between the recorded Phase 3 firmware manifest algorithm and its stored revision without changing implementation evidence or rewriting committed history.
- Outcome: Recomputed the 23-file manifest with true NUL and LF bytes, replaced the incorrect revision with art-023, preserved art-017 as a blocking tombstone, and left firmware/build/tests/report unchanged.
- Artifacts: art-023@sha256:ececef0233c46995c0983e3870a00fe3a9d94451bcb9a500c610de3254adc98b
- Validation: Two exact Python recomputations using byte values 0 and 10 produced the same digest; all 23 file hashes, art-018 through art-022, verify_phase3, build sizes, and no-Phase4 scan remain unchanged and PASS.
- Decisions: Manifest serialization is now explicitly path UTF-8 + NUL + lowercase hexadecimal file SHA-256 + LF; literal escape characters are invalid for this revision definition.
- Invalidated: art-017@sha256:11f7e139ec06c2a18ff3c379559ec608682b4513a45e3edde191a7717adeb298
- Remaining: Preserve every Hardware Validation Gate item and do not start Phase 4 without explicit user authorization.
- Next: Wait for the user's explicit Phase 4 instruction.

### 2026-08-14T05:35:56Z | phase3-gate-20260814 | Complete Phase 3 BQ transport, calibration, and hard gate

- Request: Execute Phase 2 then Phase 3 in strict order, commit the Phase 2 checkpoint before Phase 3, implement only authorized BQ transport/calibration/basic conversion, validate with real ARMCC5 and actual-C mock execution, report both gates, and stop before Phase 4.
- Outcome: Consumed the exact Phase 2 checkpoint; implemented TI-aligned BQ7694003 register transport, CRC framing, deferred ACK/NACK decisions, atomic block reads, calibration decode, and pure cell conversion; Phase 3 Hard Gate passed without entering Phase 4.
- Artifacts: art-017@sha256:11f7e139ec06c2a18ff3c379559ec608682b4513a45e3edde191a7717adeb298; art-018@sha256:7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294; art-019@sha256:f2f523dbd30c7722cc640c0d127dba5382608737e041865748e4b2b37eb6e22c; art-020@sha256:95e6a301807a2e0b0f7bc4ebb101b9ab04bd8eb447b924c833f5f2e65e9e664c; art-021@sha256:43cec17a018d6203b009543d2636e70fc1cb6cf9406ee1463d3f51b81f6192fa; art-022@sha256:e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018
- Validation: ARMCC5 actual-C test map binds current production BQ/CRC objects and mock SoftI2C; Simulator completed=1/failures=0; verify_phase3.py exit 0; ARMCC5 V5.06u7 build 960 Clean+Rebuild 0 errors/0 warnings with Code=3164, RO=268, RW=32, ZI=1896; all report hashes and 22 topics independently QA-validated.
- Decisions: Read mismatch always NACKs then attempts STOP and returns CRC_MISMATCH even if response/cleanup reports failure; output/calibration commit only after complete success; no default CC_CFG write, probe, wake, sampling, protection, or Phase 4 behavior.
- Invalidated: art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942; art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99
- Remaining: Preserve the vendor pre-main clock residual and every Hardware Validation Gate item; implement Phase 4 only after explicit user authorization.
- Next: Wait for the user's explicit Phase 4 instruction.

### 2026-08-14T05:10:27Z | phase2-gate-20260814 | Complete Phase 2 BSP, software-I2C, CRC, and hard gate

- Request: Execute Phase 2 first, including MCU BSP, TIM3, bounded software-I2C, BQ CRC, tests, ARMCC5 build, report, project-log checkpoint, and hard gate; begin Phase 3 only after exact PASS.
- Outcome: Implemented and validated Phase 2 outside docs; corrected the drifted ARMCC5 project lock; completed a full Clean+Rebuild at 0 errors/0 warnings; executed actual production soft-I2C/CRC C under the Keil simulator with completed=1/failures=0; Phase 2 Hard Gate passed.
- Artifacts: art-011@sha256:53e22e783fa141f8a608f665ca499415c110770d4ea083d4850ef138fddc9942; art-012@sha256:4851d8e4b56d7a2dab744a7508805d681c9ab0c60af35cf26b269a1a89325d99; art-013@sha256:36c0c46d2074592c7913f904efcd72193c3ab56e42612e50bfaa459875b08f79; art-014@sha256:ef8e3c735f6bf3b7635d8234089aa9d33ae45bd614fc27cf7cc7485433186bb3; art-015@sha256:3b78fdb340e5cadec61836bc6176ec7ad864cb9a36b3849dba3382648cccb364; art-016@sha256:4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34
- Validation: verify_phase2.py exit 0; Keil simulator actual-C harness completed=1/failures=0; ARMCC5 V5.06u7 build 960 Clean+Rebuild 0 errors/0 warnings with Code=3084, RO=268, RW=24, ZI=1896; map/fromelf and seven report hashes cross-checked; independent final QA PASS; no Phase 3 file existed at the checkpoint.
- Decisions: Deferred read response is frozen for CRC-aware ACK/NACK; generic 9-clock recovery is bounded but not a TI or hardware guarantee; Phase 3 may start only from the exact validated Phase 2 revision.
- Invalidated: art-007@sha256:5a86985ce4ba7836742959f23a7e9eda507d3fae7fcd97eeb48b080e5aee3b8d; art-008@sha256:720c2838a6629be447db1f33f2067339fea04c2d548e6fe8ab13ff04cc80101a; art-009@sha256:de14830be342b3239c70f1ac93495d9472f20d0d6a1bc2fb3c25418295109a26
- Remaining: Execute only Phase 3 transport/calibration/basic conversion; retain the pre-main clock residual and every Hardware Validation Gate item; do not enter Phase 4.
- Next: Create Phase 3 artifacts only after this exact log revision is committed and a final no-Phase3-file scan passes.
