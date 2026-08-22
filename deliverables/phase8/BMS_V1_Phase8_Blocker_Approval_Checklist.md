# Phase 8 Blocker Approval Checklist

> Purpose: human-executable review checklist for the two Phase 8 Hard Gate
> blockers. It mirrors the intake templates and schemas in
> `deliverables/phase8/input_templates/`.
>
> **ALL required items must pass before the corresponding blocker may move
> from MISSING/BLOCKED to candidate-for-gate-validation.**
>
> This checklist does not approve anything by itself. It does not resolve
> Blocker-1 or Blocker-2. Phase 8 Hard Gate remains `BLOCKED(2)`; Phase 9
> official implementation remains `NOT STARTED`.

Legend:

- `[ ]` unchecked item
- Evidence/reference: where the reviewer proves the requirement
- Pass condition: what must be true to tick the box

---

## Part A — Blocker-1 checklist (NTC configuration artifact)

Artifact: `BMS_V1_NTC_CONFIG` (final approved file, immutable revision).

- [ ] **B1-01 — Exact NTC identity**
  - Requirement: manufacturer, exact part number, variant.
  - Evidence/reference: `device` section of the final artifact vs BOM.
  - Pass condition: all three fields non-empty and matching the BOM part; no
    generic "10 kΩ NTC" identity.
- [ ] **B1-02 — Source revision**
  - Requirement: authoritative R-T source with exact revision/location.
  - Evidence/reference: `source` section (document title, revision,
    location, type).
  - Pass condition: source is a real vendor datasheet/calibration record at
    a traceable revision; location is page/table/record specific.
- [ ] **B1-03 — TS1 topology / schematic / BOM**
  - Requirement: TS1 channel, divider topology, schematic and BOM
    references.
  - Evidence/reference: `electrical_model.ts_channel`,
    `electrical_model.topology`, `schematic_reference`, `bom_reference`.
  - Pass condition: TS1 path is used and the topology matches the current
    software divider model (pull-up to REGOUT per the BQ TS equation);
    schematic/BOM refs resolve.
- [ ] **B1-04 — Bias resistor**
  - Requirement: nominal value and tolerance.
  - Evidence/reference: `electrical_model.bias_resistor_nominal_ohm`,
    `bias_resistor_tolerance` vs schematic/BOM.
  - Pass condition: approved board values supplied (reference constant
    10000 Ω alone is NOT approval) and consistent with the schematic.
- [ ] **B1-05 — REGOUT**
  - Requirement: reference/REGOUT voltage for the divider model.
  - Evidence/reference: `electrical_model.regout_nominal_mv` vs datasheet
    and schematic.
  - Pass condition: approved value supplied; consistent with the 3.3 V
    model (reference only) or a documented, reviewed deviation.
- [ ] **B1-06 — Integer table**
  - Requirement: discrete points as integer ohm / deci-°C.
  - Evidence/reference: `table.points[]` against the schema (integer,
    positive `resistance_ohm` within uint32_t; `temperature_decic` within
    int16_t).
  - Pass condition: all points are integers in the declared units; no
    floating point or Beta-string-only input.
- [ ] **B1-07 — Monotonicity**
  - Requirement: strictly monotonic resistance and strictly monotonic
    temperature in the opposite direction.
  - Evidence/reference: gate/tooling check (JSON Schema cannot prove it);
    `table.ordering` declaration.
  - Pass condition: tooling confirms strict monotonicity in both
    directions; declared ordering matches the data.
- [ ] **B1-08 — Coverage**
  - Requirement: declared minimum/maximum temperature endpoints.
  - Evidence/reference: `coverage.minimum_temperature_decic`,
    `coverage.maximum_temperature_decic`.
  - Pass condition: endpoints present, minimum < maximum, and they match
    the table's own extremes (or the product decision is documented).
- [ ] **B1-09 — No extrapolation**
  - Requirement: out-of-range behavior = reject, no extrapolation.
  - Evidence/reference: `coverage.out_of_range_behavior` and
    `BMS_Ntc_Interpolate` contract.
  - Pass condition: value is `reject_no_extrapolation`; the software
    contract is confirmed.
- [ ] **B1-10 — Approval identity**
  - Requirement: approver, time, approval record.
  - Evidence/reference: `approval.approved_by`, `approved_at`,
    `approval_record`.
  - Pass condition: identity/time/evidence present, evidence binds the same
    revision, status is `APPROVED`.
- [ ] **B1-11 — Artifact hash**
  - Requirement: canonical bytes SHA-256 bound to revision.
  - Evidence/reference: `approval.artifact_sha256` and the stored immutable
    copy.
  - Pass condition: computed hash of the canonical stored bytes matches the
    artifact's declared hash and the approval record.

Blocker-1 moves to candidate-for-gate-validation only when B1-01 … B1-11
all pass.

---

## Part B — Blocker-2 checklist (AFE startup/protection policy artifact)

Artifact: `BMS_V1_AFE_STARTUP_PROTECTION_POLICY` (final approved file,
immutable revision).

- [ ] **B2-01 — Exact BQ variant / datasheet basis**
  - Requirement: exact BQ7694003 variant, datasheet revision, errata
    revision or NONE, board/schematic revision, policy revision.
  - Evidence/reference: `device_basis`.
  - Pass condition: all fields present and traceable; policy revision
    matches the artifact revision.
- [ ] **B2-02 — Startup registers (SYS_CTRL1 / SYS_CTRL2 early/final)**
  - Requirement: approved values, mask semantics, allowed masks, readback
    rules.
  - Evidence/reference: `startup_policy.sys_ctrl1`,
    `startup_policy.sys_ctrl2_early`, `startup_policy.sys_ctrl2_final`.
  - Pass condition: approved values and rules present (source constants
    marked reference-only do not count); DELAY_DIS/CC_ONESHOT prohibition
    and reserved-bit policy present; startup CHG/DSG states approved.
- [ ] **B2-03 — CELLBAL startup zero**
  - Requirement: explicit approval of all-zero startup balancing and exact
    readback.
  - Evidence/reference: `startup_policy.cellbal_startup`.
  - Pass condition: `all_zero_approved = true`, all three registers 0,
    `exact_readback_required = true`, readback rule present.
- [ ] **B2-04 — CC_CFG**
  - Requirement: approved value, datasheet basis, readback rule,
    CC-enable/first-event policy.
  - Evidence/reference: `startup_policy.cc_cfg`.
  - Pass condition: value approved (0x19 reference alone is not approval),
    basis and rules present.
- [ ] **B2-05 — OV**
  - Requirement: per-cell target mV, physical delay, PROTECT3 OV code,
    quantization policy, OV_TRIP encoding policy, readback policy.
  - Evidence/reference: `hardware_protection_policy.ov`.
  - Pass condition: all fields present; `delay_register = "PROTECT3"`;
    target→code recomputation (under runtime calibration) verified by gate
    tooling.
- [ ] **B2-06 — UV**
  - Requirement: per-cell target mV, physical delay, PROTECT3 UV code,
    quantization policy, UV_TRIP encoding policy, readback policy.
  - Evidence/reference: `hardware_protection_policy.uv`.
  - Pass condition: all fields present; `delay_register = "PROTECT3"`;
    target→code recomputation verified.
- [ ] **B2-07 — OCD**
  - Requirement: target current or sense voltage, RSNS selection, threshold
    code, physical delay, PROTECT2 code, selection policy, readback policy.
  - Evidence/reference: `hardware_protection_policy.ocd`.
  - Pass condition: at least one of target_current_ma /
    target_sense_voltage_mv present; `delay_register = "PROTECT2"`;
    recomputation of code from target verified (not-below-requested rule
    documented).
- [ ] **B2-08 — SCD**
  - Requirement: target current or sense voltage, RSNS selection, threshold
    code, physical delay, PROTECT1 code, selection policy, readback policy.
  - Evidence/reference: `hardware_protection_policy.scd`.
  - Pass condition: at least one target present; **`delay_register =
    "PROTECT1"` (SCD delay is in PROTECT1, NOT PROTECT3)**; recomputation
    verified.
- [ ] **B2-09 — Rsense**
  - Requirement: nominal µΩ, source, approval basis.
  - Evidence/reference: `current_rsense_policy.rsense_nominal_uohm`,
    `rsense_source`, `rsense_approval_basis`.
  - Pass condition: approved board value from schematic/BOM/calibration;
    `BMS_RSENSE_REFERENCE_UOHM` alone is explicitly NOT approval.
- [ ] **B2-10 — Current polarity**
  - Requirement: polarity (±1), polarity definition, RSNS bit, conversion
    bases.
  - Evidence/reference: `current_rsense_policy.current_polarity`,
    `polarity_definition`, `rsns_bit`, `cc_conversion_basis`,
    `ocd_mapping_basis`, `scd_mapping_basis`.
  - Pass condition: polarity and definition approved (reference +1 is not
    approval); bases documented.
- [ ] **B2-11 — Runtime calibration**
  - Requirement: read ADCGAIN1/ADCOFFSET/ADCGAIN2 on every startup and
    XREADY recovery; read-failure and invalid-decode behavior; default
    calibration not permitted; XREADY generation binding; post-clear
    provenance; handoff owner and acceptance conditions.
  - Evidence/reference: `runtime_calibration_contract`.
  - Pass condition: all three reads required (`true`), default calibration
    `false`, behaviors present, owner and acceptance conditions present;
    runtime calibration is never replaced by constants.
- [ ] **B2-12 — XREADY recovery**
  - Requirement: full reconfiguration, pre-clear safe preparation, single
    authorized runtime W1C (ProtectTask owner, BMS_AfeStartup startup
    exception), ambiguity policy, post-clear full config/readback, settle,
    calibration provenance, first valid current-generation measurement,
    second-XREADY policy, historical latch reset policy.
  - Evidence/reference: `xready_recovery_contract`.
  - Pass condition: frozen FROZEN-07/08/09/10 alignments hold; all policy
    fields present. **OP-01 (historical latch reset) is currently OPEN: the
    field must still be supplied by an approved policy decision; a
    null/placeholder/DRAFT artifact is invalid.**
- [ ] **B2-13 — FET handoff / enable**
  - Requirement: startup CHG/DSG states; XREADY recovery inhibit; active HW
    fault directional inhibits (OV/UV/OCD/SCD/OVRD_ALERT/XREADY × CHG/DSG);
    latched-fault handling; calibration, first-valid-frame, current and
    temperature validity requirements; stale-data behavior; AFE comm
    failure behavior.
  - Evidence/reference: `fet_handoff_enable_policy`.
  - Pass condition: every field present and consistent with frozen
    architecture (FROZEN-01…FROZEN-18); open policy items (OP-01…OP-08) are
    resolved only by approved policy, not guessed.
- [ ] **B2-14 — Readback rules**
  - Requirement: readback/verification rules for every startup and
    protection register group.
  - Evidence/reference: readback fields across `startup_policy` and
    `hardware_protection_policy`.
  - Pass condition: every write has a declared readback/acceptance rule.
- [ ] **B2-15 — Approval identity**
  - Requirement: approver, time, approval record.
  - Evidence/reference: `approval.approved_by`, `approved_at`,
    `approval_record`.
  - Pass condition: identity/time/evidence present, evidence binds the same
    revision, status is `APPROVED`.
- [ ] **B2-16 — Artifact hash**
  - Requirement: canonical bytes SHA-256 bound to revision.
  - Evidence/reference: `approval.artifact_sha256` and the stored immutable
    copy.
  - Pass condition: computed hash of the canonical stored bytes matches the
    artifact's declared hash and the approval record.

Blocker-2 moves to candidate-for-gate-validation only when B2-01 … B2-16
all pass.

---

## Part C — Cross-artifact checks

- [ ] **C-01 — Same board/BQ revision**
  - Requirement: NTC and AFE artifacts reference the same
    board/schematic revision and compatible BQ7694003 basis.
  - Evidence/reference: NTC `electrical_model`/`source` vs AFE
    `device_basis`.
  - Pass condition: no revision mismatch between the two artifacts.
- [ ] **C-02 — Compatible current/temperature policy references**
  - Requirement: Rsense/current references in the AFE artifact and
    NTC/TS1 model references are consistent.
  - Evidence/reference: cross-read of both artifacts.
  - Pass condition: no contradiction between electrical models and
    current/temperature mapping bases.
- [ ] **C-03 — No contradictory revisions**
  - Requirement: datasheet, errata, schematic, policy and NTC source
    revisions are mutually consistent.
  - Evidence/reference: revision fields in both artifacts.
  - Pass condition: no field references a revision that contradicts another
    documented revision.
- [ ] **C-04 — Generated production candidate identifies exact artifact
  revisions**
  - Requirement: any generated production configuration/table candidate
    records the exact artifact revisions it derives from.
  - Evidence/reference: future generation/binding step (see
    `BMS_V1_Phase8_Future_Gate_Binding_Plan.md`).
  - Pass condition: the candidate's manifest names the exact NTC and AFE
    artifact revisions + hashes.

---

## Part D — Hardware-validation deferred checks (NOT satisfied by artifacts)

These remain `HARDWARE VALIDATION DEFERRED`. They are not part of the
software gate blockers and are tracked for the hardware phase:

- [ ] **HW-01** — actual NTC accuracy, full-range error, lot variation
- [ ] **HW-02** — actual Rsense tolerance, Kelvin routing, copper
  resistance, current gain/offset and polarity waveforms
- [ ] **HW-03** — actual AFE OV/UV/OCD/SCD trip thresholds and delay timing
- [ ] **HW-04** — actual IWDG timing/reset behavior
- [ ] **HW-05** — physical MOS conduction / FET turn-on-off timing
- [ ] **HW-06** — ALERT edge timing, W1C physical commit point, STOP
  ambiguity waveforms
- [ ] **HW-07** — I2C/brownout/thermal/EMI/ESD/power integrity behavior

---

## Status block

- [ ] Blocker-1 (NTC): all B1 items pass → candidate-for-gate-validation
- [ ] Blocker-2 (AFE): all B2 items pass → candidate-for-gate-validation
- [ ] Cross-artifact checks pass (C-01 … C-04)

Until both blockers are candidates AND the future gate (schema + semantic +
binding + wiring evidence) passes, the Phase 8 Hard Gate remains
`BLOCKED(2)` and Phase 9 official implementation remains `NOT STARTED`.
