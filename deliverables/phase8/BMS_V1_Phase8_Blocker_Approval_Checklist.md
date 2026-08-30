# Phase 8 Blocker Approval Checklist — Artifact Contract v2

This checklist reviews v2 inputs. The original v1 intake contract is
**HISTORICAL INPUT CHECKLIST**. This document records the Phase 8 input-review
contract; approved bindings and current status are incorporated into the final
Release Baseline.

## Common artifact checks

- [ ] Exact `hardware_identity` tuple is present: `BMS_V1`, hardware variant,
  board revision, schematic revision, and exact BQ7694003 ordering/variant
  code.
- [ ] NTC and AFE tuples are byte-for-byte equal as JSON string values.
- [ ] NTC TS and AFE device bases name the same BQ datasheet revision.
- [ ] File passes strict `BMS_CANONICAL_JSON_V1` parsing: UTF-8/no BOM,
  integer-only numbers, no duplicate keys, interoperable integer range,
  Unicode NFC, and no unpaired surrogate.
- [ ] No placeholder (`<REQUIRED...>`, `DRAFT`, `TBD`, `TODO`, or
  `PLACEHOLDER`) remains anywhere.
- [ ] Approval status is `APPROVED`, timestamp is strict RFC3339 UTC, and the
  declared lowercase hash equals the recomputed **canonical projection
  SHA-256**.
- [ ] A detached approval record independently matches the artifact revision,
  schema, canonical projection hash, hardware identity, repository path/blob,
  record ID, approver, and approval time.
- [ ] The final artifact contains no `current_software_reference` or other
  reference-only section.

## Blocker-1 — NTC v2

- [ ] Exact NTC manufacturer, part, variant, tolerance/curve basis, source
  title/revision/location/type, schematic net, and BOM references are reviewed.
- [ ] TS1 conversion basis includes approved bias nominal Ω, integer tolerance
  ppm, REGOUT µV, TS ADC LSB µV, and `model_confirmed = true`.
- [ ] `point_count` equals the number of points and is at least two.
- [ ] Resistance is strictly monotonic in the declared direction; temperature
  is strictly monotonic in the opposite direction; no point is duplicated.
- [ ] Units are `ohm` and `deci_C`; every numeric value is an integer in its
  schema domain.
- [ ] Coverage minimum is below maximum and both equal the table's actual
  temperature extremes.
- [ ] Out-of-range behavior is `reject_no_extrapolation`.
- [ ] Provenance identifies the source curve, generation method, and tool or
  record. The artifact table itself is authoritative.
- [ ] Future generated `BMS_NtcPoint_t` output matches ordered resistance,
  temperature, and count semantic values exactly and reaches
  `BMS_Sample_SetNtcTable`.
- [ ] Production TS conversion constants match the approved TS model.
- [ ] Physical NTC accuracy and lot/full-range behavior are indexed as interface
  observations in the integration matrix.

## Blocker-2 — AFE v2

- [ ] Wake probe, SYS_CTRL1, SYS_CTRL2 early/final, CELLBAL1/2/3, CC_CFG,
  SYS_STAT, initial settle, and CC_READY ownership fields match reviewed source
  and datasheet evidence.
- [ ] Startup CHG and DSG are both `OFF`; CC_EN is required at final handoff;
  DELAY_DIS and CC_ONESHOT are prohibited; reserved bits and readback are safe.
- [ ] OV/UV targets are approved. Delay units are source-native seconds, and
  values match the selected 0..3 register codes.
- [ ] Exactly one authoritative `current_rsense_policy.rsns_bit` (0 or 1) is
  supplied; OCD/SCD do not contain independent RSNS selections.
- [ ] OCD and SCD each use exactly one discriminated target basis: positive
  current mA or positive sense voltage mV, never both.
- [ ] OCD delay is milliseconds/code 0..7; SCD delay is microseconds/code
  0..3; threshold codes are OCD 0..15 and SCD 0..7.
- [ ] OCD/SCD selected threshold values and codes match the source RSNS table
  and the not-below-requested selection rule. Current-basis conversion uses
  exact integer/rational arithmetic from approved Rsense.
- [ ] Runtime XREADY W1C owner is ProtectTask; startup exception is
  BMS_AfeStartup; recovery coordinator is RecoveryCoordinator and is serviced
  by StateTask.
- [ ] Runtime calibration handoff owner is RecoveryCoordinator; default/static
  calibration is forbidden; generation binding and post-clear provenance are
  required.
- [ ] First-valid-measurement proof structurally requires both
  `sample_sequence` and `afe_generation`; event-only proof is forbidden.
- [ ] Generic latch clear is forbidden.
- [ ] Recovery-in-progress and action-bearing historical XREADY, SCD, and
  OVRD_ALERT latches inhibit both CHG and DSG until future policy.
- [ ] The artifact does not attempt to resolve Phase 9 OP-01 through OP-08.
- [ ] The machine mapping covers every `BMS_AfeStartupConfig_t` field, fixed
  startup register policy, and required `BMS_AfeStartup_Init`/`Step` production
  invocation.
- [ ] Production current conversion consumes the same approved Rsense and
  polarity; runtime calibration comes from device reads through
  `BMS_Sample_SetCalibration`, with no static fallback.
- [ ] Physical trip timing/threshold accuracy, current polarity waveforms,
  Rsense/Kelvin behavior, ALERT/W1C behavior, and MOS conduction are indexed as
  interface observations in the integration matrix.

## Candidate manifest checks

- [ ] The manifest binds one non-placeholder production Git commit.
- [ ] Exact NTC/AFE schema IDs, versions, repository paths, raw SHA-256 values,
  Git commits, and Git blob identities are present.
- [ ] Artifact paths, revisions, canonical projection hashes, and Git blob
  identities match the supplied files.
- [ ] Detached approval paths, record IDs, raw hashes, and Git blob identities
  match the supplied records.
- [ ] Generated NTC/AFE outputs bind the same artifact revisions/hashes and the
  same production candidate.
- [ ] Build, test, verifier, and evidence references bind that production
  candidate.
- [ ] Reviewer records that manifest preflight is neither approval nor Hard
  Gate execution.

## Status boundary

- Input-1 and Input-2: historical intake identities retained; later approved
  bindings are recorded by the final evidence chain.
- Phase 8 checkpoint: `INPUT CONTRACT RECORDED`.
- Phase 9 Architecture Core v1: `FROZEN`.
- Phase 9 implementation: `INCORPORATED IN RELEASE BASELINE`.
- Hardware interface observations: tracked by the integration matrix.
