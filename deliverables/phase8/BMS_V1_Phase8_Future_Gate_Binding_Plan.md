# Phase 8 Future Gate Binding Plan

> **Status: PLAN ONLY — NOT IMPLEMENTED.**
>
> This document plans how approved blocker artifacts will be bound into a
> future Phase 8 gate revision. It does not modify the verifier, does not
> change the current gate, does not resolve Blocker-1 or Blocker-2, and
> does not start Phase 9. Phase 8 Hard Gate remains `BLOCKED(2)`.
>
> The current gate (`firmware/Tests/verify_phase8.py`) still unconditionally
> blocks on both blockers; nothing in this plan changes that.

## 1. Future flow (planned)

```text
approved artifact received
        |
        v
artifact copied under immutable approved-input path
        |
        v
exact bytes + SHA256 bound
        |
        v
schema validation
        |
        v
semantic validation
        |
        v
production configuration/table generated or bound
        |
        v
production startup/sample wiring implemented
        |
        v
tests
        |
        v
Clean/Rebuild
        |
        v
manifest/evidence
        |
        v
Phase8 full gate
        |
        v
Codex Sol High safety review
```

Each step must record the exact artifact revision and hash it consumed;
nothing may proceed on a revision mismatch.

## 2. Planned gate checks — NTC

For the approved `BMS_V1_NTC_CONFIG` artifact:

- [ ] schema validation (`BMS_V1_NTC_Config.schema.json`)
- [ ] no placeholder/null/DRAFT remains anywhere (in particular no
  `"<REQUIRED>"`, no `null` required fields, approval status `APPROVED`)
- [ ] point count: `point_count == points.length`, >= 2, within uint16_t
- [ ] strict resistance monotonicity (ascending or descending as declared)
- [ ] opposite strict temperature monotonicity
- [ ] exact generated table equivalence: generated production
  `BMS_NtcPoint_t` table bytes equal the artifact table exactly
- [ ] artifact hash: canonical stored bytes SHA-256 matches
  `approval.artifact_sha256`
- [ ] approval binding: approval record/evidence binds the same revision
  and hash

## 3. Planned gate checks — AFE policy

For the approved `BMS_V1_AFE_STARTUP_PROTECTION_POLICY` artifact:

- [ ] schema validation (`BMS_V1_AFE_Policy.schema.json`)
- [ ] no placeholder/null/DRAFT remains anywhere (approval status
  `APPROVED`)
- [ ] explicit presence of every protection group: OV, UV, OCD, SCD (with
  `delay_register` PROTECT3 / PROTECT2 / PROTECT1 respectively)
- [ ] recompute/verify physical target → code mapping: OV_TRIP/UV_TRIP
  encoding under runtime calibration; PROTECT1/2/3 code derivation from
  physical targets/RSNS; final register bytes reproducible
- [ ] runtime calibration not replaced by constants: ADCGAIN1/ADCOFFSET/
  ADCGAIN2 remain required device reads on every startup/recovery;
  `default_calibration_permitted == false`
- [ ] exact startup policy: SYS_CTRL1, SYS_CTRL2 early/final, CELLBAL
  all-zero, CC_CFG approved values and readback rules as approved
- [ ] readback policy present for every write
- [ ] XREADY ownership matches the Phase 9 frozen contract: ProtectTask
  sole runtime W1C owner, BMS_AfeStartup pre-scheduler exception
  (FROZEN-07); recovery inhibits both FETs until COMPLETE (FROZEN-08)
- [ ] FET handoff policy present and consistent with FROZEN-01…FROZEN-18
- [ ] artifact hash: canonical stored bytes SHA-256 matches
  `approval.artifact_sha256`
- [ ] approval binding: approval record/evidence binds the same revision
  and hash

## 4. Planned gate checks — cross-artifact

- [ ] same board/BQ revision across both artifacts
- [ ] compatible current/temperature policy references (no contradiction
  between Rsense/current basis and NTC/TS1 model)
- [ ] no contradictory revision fields across datasheet/errata/schematic/
  policy/NTC source
- [ ] generated production candidate identifies the exact artifact
  revisions (NTC revision + AFE policy revision) and their hashes

## 5. Evidence binding (planned)

All of the following must bind to the same candidate:

- source revision (the approved artifacts)
- build revision (production Clean/Rebuild)
- test revision (test images/regression)
- artifact revisions (NTC + AFE policy)

A single manifest records the mapping; the gate fails on any mismatch.

## 6. Explicit non-claims

- This plan is not implemented; no verifier code has changed.
- No approved artifact exists yet; none is fabricated by this plan.
- Filling a template is not approval; approval requires the evidence and
  hash binding described here.
- Placeholder/null/DRAFT artifacts never satisfy a gate check.
- The planned gate verifies artifact→generated-configuration→production
  wiring; text hits, comments, default arrays, or policy-looking
  identifiers alone never unblock the gate.
- Hardware validation remains separate and deferred; simulator/static
  evidence is not hardware evidence.

Phase 8 Hard Gate remains `BLOCKED(2)`; Phase 9 official implementation
remains `NOT STARTED`.
