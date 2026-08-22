# BMS V1 Phase 9 Safety Architecture — Core v1 (FROZEN)

**Task:** P9-ARCH-FREEZE-001
**Prior artifacts:** P9-ARCH-BATCH-01 (architecture preparation draft); P9-ARCH-SR-001 (Codex Sol High red-team change request)
**Status:** PHASE 9 ARCHITECTURE CORE v1 — FROZEN
**Document type:** Frozen architecture core with open product-policy register
**Repository baseline:** `codex/phase8-phase9` @ `b263dd4c1e17ee6929a2b6fc155d442c1759d1e4`

> **PRODUCT-POLICY ITEMS LISTED AS OPEN REMAIN UNFROZEN.**
>
> A frozen architecture core does NOT mean:
>
> - Phase 8 Hard Gate passed.
> - Phase 9 implementation started.
> - Production thresholds approved.
>
> Only the architecture contracts in §4 are frozen. Every numeric
> product-policy item listed as OPEN in §6 remains open and unfrozen.

---

## 1. Phase Status — MUST REMAIN

| Item | Status |
|---|---|
| Phase 8 software implementation | PASS |
| Phase 8 regression/build | PASS |
| Phase 8 Hard Gate | BLOCKED (2) |
| Phase 9 official implementation | NOT STARTED |

Architecture preparation/freeze does NOT start Phase 9 implementation.

---

## 2. Scope and Non-Scope

### Scope

- Freeze the Phase 9 architecture core v1 (contracts FROZEN-01 … FROZEN-18, §4).
- Record the conceptual authoritative-safety-snapshot model (§5).
- Maintain the OPEN product-policy register (§6) — items stay OPEN, no values invented.
- Correct parameter/blocker classification (§7).
- Update Project Memory V2 (PROJECT_STATUS / TASK_BOARD / DECISIONS).

### Non-Scope (explicit)

- No production C implementation. Conceptual names in §5 (e.g.,
  `ProtectSafetySnapshot`) are NOT production code; this task does not turn
  them into C.
- No Phase 8 gate changes; Phase 8 Hard Gate remains **BLOCKED (2)**.
- No firmware / tests / verifier / docs/spec / deliverables/phase8 changes.
- No legacy memory changes (PROJECT_LOG, archive/, pending/, project-log-memory skill).
- No guessed production parameters; OPEN policy items are not resolved here.

---

## 3. Evidence Baseline (compact)

Evidence priority: current Git/source/tests/build evidence > Project Memory V2 >
docs/spec > historical materials. Where docs/spec pseudocode is simpler than
implemented Phase 4–8 source contracts, the implemented source contract wins.

| Component | Source / API | Current owner | Role under frozen core |
|---|---|---|---|
| State enum | `firmware/App/bms_state.h` (`BMS_State_t`) | shared (no writer yet) | StateTask writes `BMS_DataSnapshot_t.state`; state is operational classification only, never the FET action itself (FROZEN-01) |
| RTOS tasks | `firmware/App/app_rtos.c` / `.h` | App layer | 7 tasks; StateTask is the sole scheduler execution context that invokes the authoritative FET manager (FROZEN-04) |
| IPC objects | `app_rtos.h` (`xI2CMutex`, `xDataMutex`, `xAfeAlertSem`, queues, events) | App layer | reused as-is; `xI2CMutex` bounds every FET transaction (FROZEN-05) |
| System events | `app_rtos.h` (`EVT_SAMPLE_READY`, …) | various | `EVT_SAMPLE_READY` alone is insufficient proof of recovery measurement acceptance (FROZEN-10) |
| Measurement snapshot | `firmware/App/bms_data.h` / `.c` | SampleTask | `BMS_Data` aggregate faults are DIAGNOSTIC ONLY; not the FET manager's real-time safety authority (FROZEN-02); publication must expose AFE generation (FROZEN-10) |
| Fault IDs / summary | `firmware/App/bms_fault.h` / `.c` | utils only | stable ID set; `BMS_FAULT_ID_AFE_STALE` reserved/unasserted in Phase 9 (FROZEN-15) |
| Hardware fault capture | `firmware/App/bms_protect.c` (`BMS_Protect_Decide`, `BMS_Protect_Drain`, `Task_Protect`) | ProtectTask | Protect owns HW/AFE source faults (FROZEN-02); request/ack identity recovery (FROZEN-12); sole runtime XREADY W1C owner (FROZEN-07) |
| XREADY epoch | `bms_protect.c` (`s_xready_state`, `XreadyBindingIsCurrent`) | ProtectTask | generation is Protect-owned evidence; W1C single runtime owner (FROZEN-07); recovery-in-progress inhibits both FETs (FROZEN-08) |
| AFE startup | `firmware/App/bms_afe_startup.h` / `.c` | startup owner (pre-scheduler) | remains the separate pre-scheduler startup exception for SYS_CTRL2 and W1C (FROZEN-04 / FROZEN-07) |
| Sample / calibration | `firmware/App/bms_sample.h` / `.c` | SampleTask (Phase 8) | accepts runtime calibration handoff only under provenance contract (FROZEN-09); first-accepted-valid-measurement proof (FROZEN-10) |
| Measurement admission constants | `firmware/Config/bms_config.h` | reference config only | reference only, not production policy (FROZEN-14; OP-06 … OP-10) |
| FET primitives | `firmware/Driver/bq76940_control.h` / `.c` | driver (Phase 5) | `BQ76940_Control_SysCtrl2WithFets` is the ONLY SYS_CTRL2 compositor; full-byte readback validation required (FROZEN-05) |
| SYS_CTRL2 writer today | `bms_afe_startup.c` (SAFE_OFF path) | startup owner only | startup exception only; FET manager module is the sole scheduler-era writer of CHG/DSG state (FROZEN-04) |
| Startup sequence | `firmware/User/main.c` | main | IWDG not yet initialized; arming rules per FROZEN-14 (values OP-10) |
| docs/spec | `docs/spec/BMS_V1_统一项目方案_软件设计规格.md` | reference | reference behavior; frozen contracts here supersede where stricter |

---

## 4. Frozen Contracts — FROZEN-01 … FROZEN-18

The contracts below are binding for Phase 9 architecture and implementation.
They are the result of the Codex Sol High red-team (P9-ARCH-SR-001 change
request accepted and incorporated).

### FROZEN-01 — State classification and FET safety permission are separate

**Binding:** State classification and FET safety permission are separate
concepts. `FAULT` is not itself the FET action. Every potentially active
safety source must have either an explicit reviewed CHG/DSG action or an
explicit reviewed no-FET-effect classification. An unknown/unmapped active
safety source defaults to **BOTH INHIBITED**.

Consequences:

- `BMS_State_t` value is operational classification for behavior, reporting,
  and policy context; it is never consumed as the FET action.
- The per-fault directional action must be declared and reviewed for every
  source that can become active; a source without a reviewed action is
  treated as both-inhibiting until one is declared.
- No phase 9 code may map "state == FAULT ⇒ both off" as the safety model.

### FROZEN-02 — Authoritative fault-source ownership

**Binding:** ProtectTask owns HW/AFE source faults. StateTask owns SW
protection / DATA_STALE / RTOS_HEALTH. `BMS_Data` aggregated faults are
DIAGNOSTIC ONLY. The FET manager must not use `BMS_Data` aggregated faults as
its real-time safety authority. Each authoritative source exposes its own
coherent safety snapshot.

Consequences:

- Protect's hardware-fault state and State's software-fault state are
  separately owned; no cross-owner free writes.
- The `BMS_DataSnapshot_t.faults` aggregate remains a diagnostic/reporting
  projection, not an input to the FET decision.
- The FET manager consumes the authoritative snapshots of §5 directly.

### FROZEN-03 — Fault lifecycle and directional FET action are separate

**Binding:** Fault lifecycle (active/latched) and directional FET action are
separate. Safety outputs must represent at least `inhibit_chg_reasons` and
`inhibit_dsg_reasons`. A fault ID may be active while only one directional
inhibit is active. The FET manager consumes authoritative directional action
outputs; it must not infer actions itself from diagnostic fault bits.

Consequences:

- Each authoritative snapshot carries explicit directional reasons
  (CHG-side and DSG-side), not just "fault active".
- Active-with-single-direction (e.g., an OV-class inhibit that inhibits CHG
  only) is expressible; the FET manager honors the directional outputs
  without re-deriving them.

### FROZEN-04 — Scheduler-era SYS_CTRL2 CHG/DSG ownership

**Binding:** In the scheduler era, StateTask is the sole scheduler execution
context that invokes the authoritative FET manager. The FET manager module is
the only scheduler-era writer of SYS_CTRL2 CHG/DSG state. ProtectTask and the
Recovery Coordinator publish safety inputs and may send an urgent service
notification to StateTask; they do NOT write SYS_CTRL2 themselves.
Pre-scheduler `BMS_AfeStartup` remains a startup exception.

Consequences:

- Exactly one writer module (FET manager) and exactly one invoking task
  context (StateTask) for CHG/DSG in the scheduler era.
- Protect/Recovery influence FETs only through snapshots/inputs plus a
  notification path to StateTask; direct register writes from these contexts
  are forbidden.
- The existing pre-scheduler SAFE_OFF path in `BMS_AfeStartup` stays valid.

### FROZEN-05 — FET ENABLE transaction contract

**Binding:** Every FET ENABLE operation requires, in order:

1. authoritative safety snapshot → no inhibits;
2. acquire `xI2CMutex`;
3. revalidate safety revisions;
4. read SYS_CTRL2;
5. compose with `BQ76940_Control_SysCtrl2WithFets`;
6. write;
7. FULL BYTE readback validation;
8. revalidate safety revisions again.

Any revision change cancels acceptance of the enable. Full byte validation
includes: CC_EN required state; DELAY_DIS forced safe; CC_ONESHOT forced
safe; reserved bits safe; CHG; DSG.

Consequences:

- The two revision revalidations (before the transaction body and after
  readback) are mandatory; a revision change anywhere in between voids the
  enable.
- Readback is a full-byte comparison covering every defined bit, not just
  CHG/DSG.

### FROZEN-06 — FET manager transaction states

**Binding:** FET manager transaction states include conceptually:
`CONFIRMED_SAFE`, `CONFIRMED_APPLIED`, `UNVERIFIED`, `QUARANTINED`.
`WRITE_FINALIZATION_AMBIGUOUS` on ENABLE: NEVER blindly replay enable; enter
`QUARANTINED` / deny future enables; use fresh readback when transport
permits. A subsequent operation may attempt SAFE OFF, but not replay enable.

Consequences:

- ENABLE ambiguity is a quarantine condition, not a retry condition.
- SAFE OFF may still be attempted from quarantine; ENABLE may not be
  replayed without leaving quarantine through a proper recovery path.

### FROZEN-07 — Runtime XREADY W1C ownership and phaseful recovery

**Binding:** Runtime XREADY W1C owner: ProtectTask ONLY. Startup
`BMS_AfeStartup` retains its separate pre-scheduler W1C path. The runtime
coordinator is phaseful and StateTask-serviced. Conceptual phases:

`PRE_CLEAR_PREPARE → PRE_CLEAR_READY → WAIT_CLEAR_ACK → POST_CLEAR_CONFIG →
POST_CLEAR_SETTLE → POST_CLEAR_VERIFY → CALIBRATION_HANDOFF →
WAIT_FIRST_VALID_SAMPLE → COMPLETE` (and `FAILED`).

Protect performs the single runtime W1C only after `PRE_CLEAR_READY`.

Consequences:

- Exactly one runtime W1C actor (Protect) and one pre-scheduler W1C actor
  (AfeStartup); no third path exists.
- The Recovery Coordinator runs the phase machine; StateTask services it;
  Protect executes the W1C step only when the coordinator has reached
  `PRE_CLEAR_READY` (with the clear evidenced by a fresh observation, per
  existing strictness).

### FROZEN-08 — Recovery-in-progress safety condition

**Binding:** From first XREADY observation until `COMPLETE`:
`recovery_in_progress` is a safety condition; `inhibit_chg = true`;
`inhibit_dsg = true`. XREADY inactive alone does NOT release FET permission.
A new XREADY generation: aborts the current coordinator, invalidates old
recovery evidence, restarts recovery. Failure: both inhibited.

Consequences:

- FET permission returns only after the full recovery sequence completes;
  the inactive bit is necessary but never sufficient.
- A fresh XREADY assertion during recovery restarts the whole evidence set;
  stale evidence never survives a generation change.

### FROZEN-09 — Runtime calibration handoff provenance

**Binding:** Runtime calibration handoff requires provenance. A generation
number alone is insufficient. The recovered calibration evidence must carry
at least: `xready_generation`, `recovery_revision`, `post_clear_verified`,
`calibration`. Only the Recovery Coordinator may perform the runtime recovery
handoff. Sample accepts it only when:

- current Protect generation matches;
- XREADY inactive;
- recovery revision matches current verified recovery;
- recovery state permits handoff.

Consequences:

- The handoff is a coordinated, provenance-carrying transfer — not a
  standalone `SetCalibration` call from an arbitrary context.
- Sample-side admission checks the full provenance tuple, so a stale or
  mismatched handoff is rejected.

### FROZEN-10 — Generation-tagged measurement publication

**Binding:** Measurement publication must expose AFE generation. A valid
recovery sample acknowledgement is `sample_sequence` + `afe_generation`.
Recovery requires the FIRST ACCEPTED valid measurement from the current
recovery generation. `EVT_SAMPLE_READY` alone is insufficient proof.

Consequences:

- Consumers can tell which AFE generation a measurement belongs to.
- Recovery completion proof is the first accepted measurement carrying the
  current generation — event signaling without that acknowledged measurement
  proves nothing.

### FROZEN-11 — Sequence-tagged State decisions

**Binding:** State/SW safety decisions are tagged with
`evaluated_sample_sequence` and `evaluated_afe_generation`. Use
compare-and-publish semantics. If a newer sample was already published before
the State decision is committed: reject the stale State publication and
reevaluate. An FET enable cannot rely on a State safety decision derived from
an older measurement generation.

Consequences:

- A State decision is only valid for the measurement generation it evaluated;
  publishing it after a newer sample appeared is a stale-publish that must be
  rejected and recomputed.
- The FET manager's enable path (FROZEN-05) therefore sees only decisions
  that are current against the latest measurement generation.

### FROZEN-12 — HW_OV / HW_UV / HW_OCD recovery request/ack identity

**Binding:** HW_OV/HW_UV/HW_OCD recovery uses request/ack identity. Protect
maintains per-source assertion/source generation. State recovery
qualification includes: `fault_id`, `request_id`,
`expected_source_generation`, `evaluated_sample_sequence`,
`evaluated_afe_generation`, `qualification_revision`, and an
expiry/policy-valid marker. Protect: checks request still current; checks
generation still current; takes I2C; performs a fresh SYS_STAT read; rejects
recovery on target/blocking status; rechecks request identity; clears only
its own private active source; publishes acknowledgement. The ack is tied to
`request_id`, `source_generation`, and the Protect publication revision. Any
incompatible new event, measurement generation, transport failure, or
qualification expiry invalidates the old request/ack. SYS_STAT low alone is
NEVER physical recovery proof.

Consequences:

- Recovery is a two-party handshake with identity checks on both sides;
  stale or superseded requests are dropped.
- Protect clears only its own private active source state, never a shared
  bitmap.
- Every step between qualification and clear is re-validated; a transport
  failure or new event invalidates the whole exchange.

### FROZEN-13 — Generation-counter health model

**Binding:** Health model: one naturally aligned `uint32_t` monotonic
generation counter per required task. Each task is the sole writer of its own
counter. StateTask snapshots and compares generations. No heartbeat-bit clear
operation exists. Counter inequality across one health window is the progress
test.

Consequences:

- The snapshot→clear race of a bit-window scheme is eliminated by design:
  counters are only ever incremented by their owner and compared by
  StateTask; there is no "clear" to race with.
- Progress for a window = the task's counter advanced; equality means no
  progress in that window.

### FROZEN-14 — Liveness intervals, bounded waits, IWDG arming

**Binding:** Each required task has its own declared max liveness interval.
Event-driven tasks must use bounded waits if they participate in the required
health roster. ProtectTask must not use an infinite wait while simultaneously
being required to prove periodic health. IWDG is armed only after: health
baseline captured AND every required task observed at least one valid
advance. StateTask remains sole IWDG feeder. Numeric health periods and IWDG
timeout remain OPEN product policy.

Consequences:

- Roster membership implies a bounded execution pattern; infinite waits are
  incompatible with roster membership.
- IWDG arming is gated on observed first advances, so a task that never ran
  cannot be "healthy by baseline".
- Actual numeric periods/windows/timeouts are policy (OP-09, OP-10), not
  architecture.

### FROZEN-15 — AFE_STALE reserved; freshness and transport classes

**Binding:** `BMS_FAULT_ID_AFE_STALE` remains reserved/unasserted in Phase 9.
Measurement freshness is expressed by `DATA_STALE`; transport problems by
`AFE_COMM` / `AFE_CRC`.

Consequences:

- No Phase 9 code asserts the AFE_STALE ID; the stale class belongs to
  DATA_STALE (State-owned) and the transport class to AFE_COMM/AFE_CRC
  (Protect-owned).

### FROZEN-16 — No generic latch clear

**Binding:** A generic clear-all-latched-fault production API is forbidden.
Latch clear must be source-specific, policy-specific, and evidence-gated.

Consequences:

- Every latch-clearing path is tied to a specific source, a specific frozen
  policy, and the evidence that policy requires; no blanket reset helper may
  exist in production code.

### FROZEN-17 — Action-bearing latches until reset policy frozen

**Binding:** Until explicit reset policies are frozen, `HW_SCD`,
`AFE_OVRD_ALERT`, `AFE_XREADY`, and a continuous AFE_COMM latch (if
introduced) remain ACTION-BEARING and BOTH-FET-INHIBITING. Technical source
recovery may clear active while the historical latch remains. That does NOT
by itself authorize FET re-enable.

Consequences:

- These four classes hold both FETs off while latched, regardless of
  technical source recovery.
- Clearing the active condition is not FET permission; re-enable requires the
  full permission path (FROZEN-05) plus any future frozen reset policy
  (OP-01 … OP-04).

### FROZEN-18 — Hardware evidence boundary

**Binding:** When BQ I2C is unavailable, software may report: requested OFF;
enable inhibited; physical state UNVERIFIED. Software must NOT claim physical
MOS OFF. SYS_CTRL2 readback proves only AFE register-level state, not
physical MOS conduction.

Consequences:

- Failure reports use the "UNVERIFIED" vocabulary; "physical MOS off" is
  never asserted by software.
- Register-level readback evidence is labeled as register-level only.

---

## 5. Authoritative Safety Snapshot Model (conceptual)

The FET manager consumes the authoritative snapshots below directly.
`BMS_Data` aggregate remains diagnostic/reporting only (FROZEN-02/03).

These names are conceptual models for Phase 9; this task does NOT turn them
into production C.

### ProtectSafetySnapshot

- source faults (HW/AFE)
- `inhibit_chg_reasons`
- `inhibit_dsg_reasons`
- source/publication revision
- HW source generations

Owner: ProtectTask. Its revision and generations are what FROZEN-05
revalidation and FROZEN-12 identity checks compare against.

### StateSafetySnapshot

- SW/health faults
- `inhibit_chg_reasons`
- `inhibit_dsg_reasons`
- `evaluated_sample_sequence`
- `evaluated_afe_generation`
- publication revision

Owner: StateTask. Compare-and-publish per FROZEN-11: stale publications
(evaluated against an older sample than already published) are rejected and
reevaluated.

### AfeRecoverySnapshot

- recovery phase
- `xready_generation`
- `recovery_revision`
- inhibit status
- calibration-evidence status
- first-valid-sample acknowledgement

Owner: Recovery Coordinator (serviced by StateTask, W1C executed by Protect
per FROZEN-07). Reflects FROZEN-08/09/10: recovery-in-progress inhibits both
FETs; handoff carries provenance; completion is proven by the first accepted
valid measurement of the current generation.

### Consumption rule

- FET manager: snapshot inputs only; no inference from diagnostic fault bits;
  no direct Protect/Recovery register writes (FROZEN-02/03/04).
- `BMS_Data` aggregate: reporting/diagnostics; never the real-time safety
  authority (FROZEN-02).

---

## 6. OPEN Policy Items — REMAIN OPEN (UNFROZEN)

The following product-policy items are explicitly NOT frozen. No values are
invented or resolved in this document.

| ID | Item |
|---|---|
| OP-01 | XREADY historical latch reset authority/evidence |
| OP-02 | SCD reset authority/evidence |
| OP-03 | OVRD_ALERT reset authority/evidence |
| OP-04 | continuous AFE_COMM definition + latch/reset policy |
| OP-05 | OCD escalation / latch policy |
| OP-06 | SW OV/UV/OC production thresholds/hysteresis/debounce |
| OP-07 | temperature direction/cutoff/hysteresis/delay |
| OP-08 | DATA_STALE directional FET action |
| OP-09 | required-task health roster / exact liveness windows |
| OP-10 | IWDG hardware timeout / arming timing values |

While OP-01 … OP-04 are open, FROZEN-17 governs behavior (action-bearing,
both-inhibiting). While OP-06 … OP-10 are open, reference values in
docs/spec and `bms_config.h` remain reference only.

---

## 7. Parameter / Blocker Classification (corrected)

The previous draft's classification is corrected as follows. Phase 9
product-policy items are NOT moved into Phase 8 Blocker-2.

### Phase 8 Blocker-1

- approved immutable production NTC curve/table artifact
- NTC conversion domain

### Phase 8 Blocker-2

- approved Rsense/current mapping
- current polarity/mapping
- AFE HW OV/UV/OCD/SCD targets/delays
- runtime calibration handoff policy
- complete AFE startup policy
- XREADY recovery policy
- FET enable/startup policy

### Phase 9 product-policy OPEN (UNFROZEN, §6)

- SW OV/UV/OC thresholds/hysteresis/debounce (OP-06)
- temperature protection policy (OP-07)
- DATA_STALE action (OP-08)
- OCD escalation (OP-05)
- health roster/windows (OP-09)
- IWDG policy values (OP-10)

### Hardware validation (separate, DEFERRED)

- actual NTC accuracy
- actual Rsense/current accuracy
- actual AFE trip thresholds/timing
- actual IWDG timing/reset
- physical MOS conduction
- ALERT / I2C / brownout / thermal / EMI behavior

No guessed parameters are recorded anywhere in this document.

---

## 8. Red-Team Outcome and Resolution Map

Codex Sol High red-team of the draft (P9-ARCH-SR-001) produced a CHANGE
REQUEST. The change request was accepted and incorporated; the result is the
frozen core in §4–§5. Previously OPEN draft items resolved by the freeze:

| Draft item | Resolution |
|---|---|
| AD-01 / SR-01 XREADY W1C owner | FROZEN-07 (Protect sole runtime W1C; AfeStartup pre-scheduler exception) |
| AD-02 post-clear full-config owner | FROZEN-07 (phaseful runtime coordinator, StateTask-serviced) |
| AD-03 SCD latch reset | OPEN → OP-02; meanwhile FROZEN-17 |
| AD-04 OVRD_ALERT reset | OPEN → OP-03; meanwhile FROZEN-17 |
| AD-05 SW_OC_CHARGE threshold | OPEN → OP-06 |
| AD-06 temperature limits | OPEN → OP-07 |
| AD-07 OCD escalation/latch | OPEN → OP-05 |
| AD-08 watchdog timeout | OPEN → OP-10 |
| AD-09 physical MOS-off boundary | FROZEN-18 (documented limitation) |
| AD-10 FET manager placement | FROZEN-04/05/06 (sole scheduler-era writer; StateTask invoking context) |
| AD-11 heartbeat atomicity | FROZEN-13 (generation counters; no clear operation) |
| AD-12 fault snapshot publication | FROZEN-02/03 + §5 (authoritative snapshots; BMS_Data diagnostic only) |
| AD-13 AFE_STALE owner | FROZEN-15 (reserved/unasserted; DATA_STALE + AFE_COMM/CRC) |
| AD-14 calibration handoff | FROZEN-09/10 (provenance; first accepted valid measurement) |
| SR-02 FET single-writer + failure behavior | FROZEN-04/05/06 |
| SR-03 cross-owner HW fault clearing | FROZEN-02/12 (private source state; request/ack identity) |
| SR-04 latched reset policy | OP-01 … OP-04 remain OPEN; FROZEN-16/17 govern meanwhile |
| SR-05 heartbeat atomicity + IWDG | FROZEN-13/14; numeric values OP-09/OP-10 |

---

## 9. Phase 9 Implementation Dependency Note

Implementation planning (draft batches) is unchanged in substance: Batch A
(state engine + SW protection + recovery-qualification framework), Batch B
(FET permission aggregation + FET manager + XREADY recovery integration),
Batch C (task health + IWDG + integration). Sequencing obligations:

- FET manager work must follow FROZEN-04/05/06; XREADY recovery work must
  follow FROZEN-07/08/09/10.
- IWDG enablement must follow FROZEN-13/14 and the OP-09/OP-10 policy inputs.
- No batch may begin official implementation while Phase 8 Hard Gate remains
  BLOCKED (2) under the current process rule; this freeze does not start
  implementation.

---

## 10. Document Status and Freeze Record

- **Status: PHASE 9 ARCHITECTURE CORE v1 — FROZEN.**
- Prior status: DRAFT READY FOR ARCHITECT REVIEW — NOT FROZEN (P9-ARCH-BATCH-01).
- Red-team: Codex Sol High — P9-ARCH-SR-001 CHANGE REQUEST accepted and incorporated.
- Frozen scope: §4 contracts FROZEN-01 … FROZEN-18 and the §5 snapshot model.
- **PRODUCT-POLICY ITEMS LISTED AS OPEN REMAIN UNFROZEN** (§6, OP-01 … OP-10).
- A frozen architecture core does NOT mean: Phase 8 Hard Gate passed; Phase 9
  implementation started; production thresholds approved.
- Phase 8 Hard Gate remains **BLOCKED (2)**; Phase 9 official implementation
  remains **NOT STARTED**.
- No production parameters were guessed; no production code changed.
