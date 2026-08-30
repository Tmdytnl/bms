# BMS V1 Codex Phase 7 Review

## 1. Baseline

| Item | Value |
|---|---|
| Source branch | `origin/dsh/phase7` |
| Baseline commit | `e2022e1735e331edb1bcca6ffaa589d28c2097cd` |
| Target branch | `codex/review-phase7` |
| Review date | 2026-08-19–20 (Asia/Shanghai) |
| Host | Windows, repository `D:\AI\ai_day\codex\Bms_shop_day` |
| Production toolchain | Keil uVision 5.38; ARMCC5 5.06 update 7 build 960 |
| Verification | ARMCC5 compile/link, Keil Clean/Rebuild, Keil Simulator, Python 3 verifier |

Review scope is the Phase 4 through Phase 7 implementation, tests, reports,
Keil integration, validated errata, and project-memory reconciliation. This
document records that review checkpoint; the repository's current project
status is defined by the accepted BMS V1 Release Baseline.

Primary silicon reference: `docs/reference/TI/01_TI_BQ769x0_Datasheet_SLUSBK2I_EN.pdf`
(Rev. I), especially SYS_STAT/SYS_CTRL2 and protection threshold tables.

## 2. Recorded review checkpoint

- Phase 1–3 evidence is retained at tag `phase3-validated` (`83bd3be`).
- Phase 7 review input is retained at `e2022e1`.
- This report records the repaired Phase 4–7 checkpoint and its reproducible
  ARMCC5, Simulator, static-analysis, and regression evidence.
- Later phases completed the remaining architecture and verification chain;
  the authoritative current status is the accepted Release Baseline.

The numbered `verify_phase1.py` through `verify_phase7.py` files describe
historical phase checkpoints. Later-phase source evolution makes several of
their frozen input hashes fail by design. They are retained as historical
evidence; this review checkpoint uses `build_phase7_review.ps1` and
`verify_phase7_review.py`.

## 3. Findings

The severity below describes the baseline defect before this review. “Fixed”
means fixed in this checkpoint and covered by the evidence named in the final
column. Final project disposition is recorded in the Release Baseline.

| ID | Severity | Affected area | Problem, impact and root cause | Review action | Evidence/status |
|---|---|---|---|---|---|
| C-01 | Critical | `App/bms_protect.c`, `User/main.c` | After consuming the rising-edge semaphore, mutex/read/budget failure returned to an infinite semaphore wait. A continuously high ALERT produces no second rising edge, so a protection event could remain permanently unserviced. EXTI was also enabled before the semaphore and, even after reordering objects, before `xPortStartScheduler` initialized the Cortex-M FromISR priority validator. | Added an explicit service result and delayed task-level pending retry; each attempt has a four-read budget, releases the I2C mutex, checks the active pin, and never requires a new edge. ProtectTask now enables EXTI only in its first post-scheduler context and directly seeds work from an already-high PB1. | Fixed; real Task/ISR bodies plus H-05 Cases A/B/C and startup-high execute with zero failures. |
| C-02 | Critical | `App/bms_protect.c`, `Driver/bq76940.[ch]` | The baseline read CC, set CC_READY in `clear_mask`, then called `BMS_Protect_Decide`, which reset the mask to zero. CC_READY therefore was not W1C; repeated drain reads could enqueue the same hardware sample four times and then sleep with ALERT high. Payload+CRC ACK followed by STOP failure also has no documented BQ7694003 register-commit point. | Decision runs before CC handling. A `s_cc_clear_pending` state retries only definitely rejected clears. `WRITE_FINALIZATION_AMBIGUOUS` is neither success nor definite rejection: requested W1C bits are diagnosed and quarantined until observed low, with no blind W1C replay or duplicate CC enqueue. | Fixed and retained in the final architecture: combined-bit, rejected-write retry, continuously-high ambiguous-finalization quarantine, observed-low retirement, diagnostics, and no-duplicate regressions pass. |
| H-01 | High | `Driver/bq76940_control.c` | The SCD RSNS=1 table was `[6,44,67,89,111,133,155,178]`; TI Rev. I Table 8-9 is `[44,67,89,111,133,155,178,200]`. A requested 111 mV encoded code 4 instead of code 3, producing 133 mV (33.25 A at 4 mΩ) rather than 111 mV (27.75 A). Tests and verifier copied the same bad oracle. | Corrected the table and exercised all eight entries in both RSNS ranges with an independent fixed vector. The 111 mV/100 µs PROTECT1 value is now `0x8B`. | Fixed; Phase 5 OCD/SCD suite passes. |
| H-02 | High | `App/bms_protect.c` | Queue-full replacement was silent and the baseline did not expose exact overflow/missed diagnostics. | Added saturating overflow, irrecoverably-dropped-sample, and replacement-enqueue-failure counters, a latched diagnostic, `EVT_CC_QUEUE_OVERFLOW`, and a scheduler-protected drop-one/enqueue-newest operation. A failed replacement does not W1C and the hardware sample is retried. | Fixed at the producer boundary; first, repeated, latest/oldest, concurrent-consumer exclusion, and replacement-failure vectors pass. No Phase 7 consumer/reporting policy exists yet. |
| H-03 | High | `App/bms_protect.c` | Baseline XREADY “recovery” used hard-coded reference thresholds, omitted required settling/reinit/readback/group verification, cleared the history latch, and could overwrite future authoritative configuration. | Removed the false recovery. XREADY remains active+latched and FET requests remain off unless an externally supplied, bounded authoritative recovery hook confirms the complete contract; only then is XREADY W1C, last. | Safe boundary fixed at this checkpoint; the later phaseful generation/revision recovery implementation and its regression evidence are incorporated in the final Release Baseline. |
| H-04 | High | Historical Phase 7 tests/verifier/report | `Test_Phase7_CcQueue` and `Test_Phase7_Xready` returned zero without executing production code; the map linked only tiny stubs, while the report declared H-02/H-03/H-05 PASS. The ignored AXF and absolute paths made a fresh clone unable to reproduce the claim. | Replaced stubs with deterministic production-C execution, added explicit map-symbol/freshness checks, relative Simulator paths, and a checked ARMCC5 build runner. | Fixed at this checkpoint; the repaired evidence is retained in the final verification chain. |
| M-01 | Medium | `Driver/bq76940_control.c` | OV/UV decode subtracted ADCOFFSET although TI’s voltage equation adds it. A +30 mV calibration caused a 60 mV decode error relative to the programmed threshold. | Corrected the sign, reused full calibration validation, and added positive-offset round-trip vectors. | Fixed; trip suite passes. |
| M-02 | Medium | `Driver/bq76940_control.c` | SYS_CTRL2 FET composition preserved bits 5..2, replaying the CC_ONESHOT command and reserved bits from readback. During review, preserving `DELAY_DIS` was also rejected because it bypasses protection delays for factory testing. | Preserve only production `CC_EN` (`0x40`); force `DELAY_DIS`, CC_ONESHOT, and reserved bits low; NULL request is fail-safe FET-off. | Fixed; per-bit, factory-test-bit, and NULL regressions pass. |
| M-03 | Medium | `Driver/bq76940_control.[ch]` | PROTECT1/2/3 composers silently masked invalid codes, potentially converting invalid configuration into a different, live protection setting. The report incorrectly claimed a status return. | APIs now return `BQ76940_Status_t`, validate every field, and leave output unchanged on failure. | Fixed; all upper-bound/null vectors pass. |
| M-04 | Medium | `App/bms_protect.c` | CC read/W1C/SYS_STAT failures were incompletely observable and CRC was collapsed into generic communication failure. | Centralized transport status mapping to AFE_COMM or AFE_CRC and sets `EVT_FAULT_PRESENT`; successful SYS_STAT communication clears those active transport indicators only when no W1C-finalization ambiguity remains unresolved. | Fixed; timeout/NACK/CRC and ambiguity paths pass. |
| M-05 | Medium | `App/app_rtos_hooks.c`, `User/main.c` | Fatal hooks and safe-idle comments promised an interrupt-disabled halt, but both paths only spun forever. Interrupts could continue mutating state or enter RTOS ISR APIs during a fatal/pre-scheduler state. | RTOS fatal hooks now call `taskDISABLE_INTERRUPTS()` and main safe-idle globally disables IRQs before halting. | Fixed; compiled in both review/production images and statically gated. |
| L-01 | Low/evidence | Phase 6 report and harness | The historical report added the already-included MSP/C heap to map ZI a second time, and the harness created all IPC objects twice before creating tasks. Neither result represented a production-equivalent single initialization. | Current harness creates one object set and reuses it for task construction. This report uses the fresh production map and does not rewrite the historical report. | Corrected review evidence; runtime stack high-water remains unmeasured. |
| S-01 | Spec gap | UART | V1 specifications require debug UART1 on PA9/PA10 at 115200 and name `bsp_uart.c/.h`; no validated erratum supersedes it. The Phase 2 report and current 32-file target contain no USART/UART implementation. | No UART was added because it is outside this repair scope. | **UART REQUIRED — IMPLEMENTATION MISSING.** Must be scheduled before V1 completion. |

No confirmed Phase 4 measurement defect was found. The Phase 3 BQ transport
API/source was necessarily extended for the Phase 7 W1C finalization boundary.
The new status, appended to preserve every validated Phase 3 status ordinal,
changes only the payload+CRC-ACKed/final-STOP-failed outcome; existing callers
still see a non-OK result, while ProtectTask avoids claiming either commit or
rejection. Normal write, data NACK, CRC NACK, and STOP-failure
transport regressions were rebuilt and executed.

## 4. H-05 ALERT retry / lost-edge result

The repaired flow is:

1. Main creates all RTOS objects and tasks but leaves PB1 EXTI/NVIC disabled.
2. After `xPortStartScheduler` initializes the FreeRTOS port, the highest-
   priority ProtectTask configures/enables EXTI1, then samples active-high PB1
   and retains local pending work if it is already high.
3. ISR only clears EXTI pending, gives the semaphore, and requests a yield; it
   performs no I2C transaction.
4. `Task_Protect` consumes the semaphore once. If a service attempt cannot take
   the mutex, cannot read/write, exhausts its four-iteration drain budget, or
   sees ALERT still high, it retains local pending state.
5. A pending retry delays 10 ms, performs another bounded service attempt, and
   releases the mutex between attempts. It does not wait for a second edge.

Regression coverage:

- Case A: first mutex take fails after the logical wake; a second service call
  drains OV and returns idle without another edge.
- Case B: four nonzero snapshots exhaust the drain budget and return retry; a
  high pin after an otherwise empty read also returns retry.
- Case C: OV is cleared, UV appears on the next read, and both independent W1C
  writes and faults are observed.
- Additional paths: direct ISR execution, SYS_STAT timeout, rejected W1C NACK,
  ambiguous write finalization, CC CRC mismatch, and repeated CC_READY.

The test image executes the production drain/service functions, post-scheduler
EXTI enable/startup-high seed, the actual `Task_Protect` loop through
semaphore-consume → mutex failure → 10 ms delay → retry → drain, and the actual
ISR body, using deterministic RTOS/BQ/BSP fakes.
It does not run a preemptive scheduler, real software-I2C electrical timing, or
physical EXTI; those remain hardware/integration validation items.

## 5. H-02 CC queue diagnostics result

The policy remains “preserve newest, drop exactly one oldest.” The full-check,
discard, and replacement enqueue occur while the task scheduler is suspended,
so a concurrent task consumer cannot make the wrapper discard a second entry.

Observable state now includes:

- saturating `cc_queue_overflow_count` (one per overflow operation);
- saturating `cc_sample_missed_count` (one per irrecoverably dropped queued
  sample);
- saturating `cc_enqueue_failure_count` (failed replacement attempts; the
  hardware sample remains pending and is retried);
- latched `cc_queue_overflow_latched`;
- `EVT_CC_QUEUE_OVERFLOW` in the system event group.

If the replacement enqueue fails after the oldest entry was dropped, the
diagnostic records one dropped sample and one enqueue failure, CC_READY is not
W1C, and the bounded drain retries the still-pending hardware reading. A
definitely rejected W1C retries only the clear. If payload/CRC are ACKed but
STOP finalization fails, the outcome is not called committed: the requested
bit is quarantined, no W1C or enqueue is replayed while it remains high, and an
observed-low read is the only software retirement point.

Observable ambiguity state includes a saturating transaction counter, a
CC-specific event-identity ambiguity counter, a current quarantined-bit mask,
and a latched history flag. A continuously high CC_READY after ambiguous
finalization can represent either the old uncleared event or a newer event;
software does not invent an identity and exact zero-loss is not claimed.

## 6. Phase 4 review result

Reviewed 13S mapping, skipped VC9/VC14 channels, adjacent/block transaction
contracts, calibration dependency, cell/BAT/CC/TS conversion widths and signs,
rounding/range behavior, transactional output, CRC/I2C failure propagation,
and stale/partial-read boundaries.

Result: no confirmed measurement-layer production change was required. The
shared Phase 3 BQ write transport gained the finalization-ambiguous outcome
needed by Phase 7. A freshly built ARMCC5 Phase 4 image executed mapping,
measurement, negative-result transactionality, TS-boundary, and real transport
commit tests with zero failures. The production-C Simulator method and analog/
interface observations retain separate evidence identities.

## 7. Phase 5 review result

Reviewed PROTECT1/2/3 selection/composition, OV/UV trip conversion, OCD/SCD
tables and delays, CELLBAL mapping, CHG/DSG arbitration, fail-safe defaults, and
the single-writer foundation. Four confirmed defects were fixed: SCD RSNS=1
table, OV/UV offset sign/validation, invalid composer masking, and SYS_CTRL2
command/reserved replay.

No Phase 4–7 application module writes `SYS_CTRL2` directly. The decision→FET
compositor boundary is exercised for OV, UV/OCD, and SCD/XREADY, including
CC_EN preservation and factory/command/reserved-bit clearing. This remains a
pure arbitration foundation consumed by the later Phase 9 FET Manager. The
production link removes unused full measurement/control routines at this
checkpoint; dedicated review images execute them while production Phase 7
links the CC-read/protect subset.

## 8. Phase 6 review result

Reviewed seven tasks, priorities `5/4/3/3/2/2/2`, stack sizes, queue depths,
mutexes/semaphore/event group, FreeRTOS assert/stack-overflow configuration,
heap, ISR API legality, PriorityGroup_4, Cortex-M exception mapping, startup
ordering, fatal interrupt shutdown, and scheduler-return fail-safe behavior.

The fresh Phase 6 image creates one set of seven IPC objects and all seven
tasks with zero failures. This deterministic creation test is complemented by
separate scheduler, stack high-water, and runtime observation evidence. Fresh
production link-time RAM is `RW 200 + ZI 10432 = 10632` bytes, leaving 9848
bytes of the 20 KiB SRAM address space.

## 9. Phase 7 review result

- PB1/EXTI1 startup ordering: EXTI stays disabled until first ProtectTask
  context after port initialization; startup-high and ISR bodies execute
  against deterministic RTOS/BSP fakes.
- SYS_STAT: every bit handled independently; only successfully consumed bits
  enter W1C; drain re-reads for newly arriving bits.
- CC_READY: sampled/enqueued before W1C; overflow visible; definite rejection
  retries only W1C, while ambiguous finalization quarantines event identity.
- OV/UV/OCD: captured as unresolved active events and FET inhibits; they are
  recovery-eligible only through the Phase 9 measurement/freshness/threshold/
  hysteresis/delay/policy owner. SYS_STAT low never clears them.
- SCD: captured active+latched and not auto-cleared in Phase 7.
- OVRD_ALERT: independent active+latched fault, both FET requests off, event,
  and W1C path retained; the later recovery policy is incorporated into the
  final Release Baseline.
- XREADY: no immediate clear and no invented configuration. Active+latched and
  FET-off remain until the full external recovery contract succeeds and W1C
  finalization is confirmed; its latch remains for Phase 9 explicit reset.
- Fault readers receive a scheduler-protected same-generation `active+latched`
  snapshot; task-context writers publish both words under the same protection.
- H-05: task-level pending retry no longer depends on a new rising edge.

Result: reviewed Phase 7 checkpoint with reproducible diff/build/test evidence;
its repaired architecture and verification records are incorporated into the
final Release Baseline.

## 10. UART disposition

**UART1 DIAGNOSTIC PATH — INCORPORATED IN RELEASE BASELINE**

Evidence in the unified V1 specification includes the V1 debug-UART scope,
PA9/PA10 pin assignment, required `bsp_uart.c/.h`, UART1 115200 startup step,
and Phase 2 file list. The validated errata do not supersede it. The Phase 2
report recorded the earlier USART scope boundary. The later `bsp_uart` and
read-only 1 s diagnostic snapshot integration, tests, and target binding are
incorporated into the final Release Baseline.

## 11. Tests

Run from repository root:

```powershell
& firmware\Tests\build_phase7_review.ps1
```

This no-switch command is the authoritative gate; `Skip*` modes are diagnostics
and do not produce a verifier PASS. It requires Windows, Python 3, licensed
Keil uVision 5.38, and ARMCC5 5.06u7 (default paths or explicit parameters).
The command rebuilds ignored AXFs from source, so a fresh clone does not depend
on checked-in binaries. It then runs three images in one Keil Simulator session:

| Image | Executed scope | Result |
|---|---|---|
| Phase 4 review | mapping, atomic cell window, BAT, signed CC, TS, CRC/I2C/transactional errors, write finalization boundary | completed=1, failures=0 |
| Phase 6 review | one IPC object set, event bits, queue layouts, priorities/stacks, seven task creation | completed=1, failures=0 |
| Combined Phase 5/7 review | trip/table/composer/FET/CELLBAL plus cross-API inhibit, decision, real Task/ISR bodies, CC queue, H-05 retry and XREADY gates | completed=1, all nine suite counters=0 |

Key new regression vectors include first/repeated queue full, exact oldest/newest
sequence, replacement enqueue failure, rejected W1C retry without duplicate
sample, ambiguous W1C/STOP quarantine across continuously high CC_READY,
observed-low quarantine retirement, ambiguity counters/current mask,
Task-level mutex timeout/delayed retry, post-scheduler EXTI enable, startup-high
without an ISR edge, drain budget, active-pin-high retry,
event arrival during drain, ISR execution, transport timeout/NACK/CRC, XREADY
absent/failing/successful recovery hook and ambiguous-STOP outcome, all SCD table
entries, invalid composer fields, calibration offset sign, negative transactional
results, TS denominator boundary, and SYS_CTRL2 factory/command/reserved bits.

`verify_phase7_review.py` checks source contracts, project integration, linked
symbols, artifact freshness, test counters, build warnings/errors, memory fit,
and the UART implementation state. Current result:

`RESULT: PHASE7 REVIEW VERIFICATION PASS`

## 12. Build evidence

Production Clean/Rebuild actually executed in the current environment:

- 32 compilation units;
- ARMCC5 5.06 update 7 build 960;
- `0 Error(s), 0 Warning(s)`;
- `Code=18908`, `RO-data=268`, `RW-data=200`, `ZI-data=10440`;
- total ROM `19376` bytes; link-time RW+ZI `10640` bytes.

Plain-text evidence:

- `firmware/Project/Keil/Build/BMS_V1_Codex_Phase7_build.log`
- `firmware/Project/Keil/Listings/BMS_V1.map`
- `firmware/Tests/Build/Phase7Review/phase7_review_build.log`
- `firmware/Tests/Build/Phase7Review/phase4_review_tests.map`
- `firmware/Tests/Build/Phase7Review/phase6_review_tests.map`
- `firmware/Tests/Build/Phase7Review/phase7_review_tests.map`
- `firmware/Tests/Build/Phase7Review/phase7_review_simulator.log`
- `firmware/Tests/Build/Phase7Review/verify_phase7_review.log`

The test AXFs/objects remain ignored and are reproducibly regenerated by the
checked-in PowerShell runner. Maps/logs embed checkout paths and timestamps, so
the result is semantically reproducible rather than byte/hash invariant across
different checkout directories.

## 13. Physical interface observations recorded by this historical review

The review recorded the following physical-interface observation dimensions:

`BQ7694003 W1C commit behavior when STOP finalization fails`

- actual BQ7694003 identity, CRC-enabled address, 13S VC/VCxB wiring, VC9/VC14
  shorts, supply/REGSRC/REGOUT/CAP/RC implementation, and group population;
- real ALERT voltage, RC, rise/fall/stuck-high behavior and EXTI timing;
- real XREADY/SHIP→NORMAL settling, authoritative full reinit, calibration and
  register readback on hardware;
- current polarity and actual shunt value/tolerance; OV/UV/OCD/SCD analog trip
  accuracy and MOS turn-off behavior;
- cell/BAT/TS accuracy, NTC curve, temperature calibration, and PA8 wake path;
- preemptive scheduling timing, I2C contention, ISR stress, stack high-water,
  minimum free heap, and long-duration queue/ALERT behavior.

Simulator/mock evidence must not be described as any of the above.

## 14. Review findings carried into later phases

1. The Phase 7 checkpoint established the fail-safe XREADY boundary. The final
   architecture supplies the phaseful recovery contract with generation and
   revision identity, verified publish/ack handling, and XREADY-last semantics.
2. Debug UART is integrated in the final diagnostic publication path.
3. The deterministic queue fake proves the production wrapper sequence and
   scheduler-protection calls, but does not replace a running-scheduler stress
   test or target ISR concurrency test.
4. A definitely rejected CC W1C retains the old pending marker to prevent
   duplicate integration. An ACKed-payload/CRC plus failed STOP is quarantined
   instead of replayed. If CC_READY remains high, old/new event identity is not
   distinguishable: no duplicate is enqueued, but a later conversion may
   coalesce. Saturating ambiguity diagnostics make this bounded identity limit
   observable without inventing or replaying an event identity.
5. The final baseline incorporates the later SOC, State, diagnostics, and
   publication consumers built on the Phase 7 producer boundary.
6. This report remains the directly addressable Phase 7 review record; current
   project acceptance is stated only by the final Release Baseline.

## 15. Recorded continuation decision

The review approved continuation into Phase 8 based on the repaired Phase 4–7
checkpoint: production-C test images executed cleanly, the production target
rebuilt with zero errors/warnings, and unresolved XREADY state was held
fail-safe rather than falsely cleared.

That continuation is now historical evidence. The repository's final project
status, frozen architecture, verification chain, and artifact identities are
defined by `deliverables/release/BMS_V1_Release_Baseline.md`.
