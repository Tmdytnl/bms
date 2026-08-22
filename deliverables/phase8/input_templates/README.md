# Phase 8 Blocker Intake Package — README

> This directory contains the **machine-fillable intake package** for the two
> Phase 8 Hard Gate blockers. It is gate **preparation**, not gate resolution.

## 1. These are templates, NOT approved inputs

The `.template.json` files in this directory are DRAFT fill-in forms. They
contain `null`, `"<REQUIRED>"`, and `"DRAFT"` placeholders on purpose.
Nothing in this directory is an approved production artifact. No template
file, no schema file, and no markdown checklist is a production parameter.

## 2. Completing a template does NOT itself pass the gate

Filling every field of a template does not pass the Phase 8 Hard Gate. The
gate remains `BLOCKED(2)` until, at minimum, the future gate revision
explicitly binds the approved immutable artifact revision, its canonical
hash, its approval evidence, and the production wiring that consumes it.
See `BMS_V1_Phase8_Future_Gate_Binding_Plan.md`.

## 3. Final artifacts require approval evidence and an immutable hash

A final artifact must carry:

- an immutable revision identifier;
- the exact canonical bytes (UTF-8, LF, fixed key/order rules, no floating
  point) and their SHA-256;
- an approval record (approver identity, ISO-8601 time, evidence ID) that
  binds the same revision.

An unapproved or un-hashable file is not a final artifact.

## 4. Blocker-1 vs Blocker-2 mapping

| Blocker | Artifact | File pair |
|---|---|---|
| Blocker-1: production NTC curve/table (incl. NTC conversion domain) | `BMS_V1_NTC_CONFIG` | `BMS_V1_NTC_Config.template.json` / `BMS_V1_NTC_Config.schema.json` |
| Blocker-2: AFE startup/protection policy (Rsense/current, OV/UV/OCD/SCD, calibration handoff, XREADY recovery, FET policy) | `BMS_V1_AFE_STARTUP_PROTECTION_POLICY` | `BMS_V1_AFE_Policy.template.json` / `BMS_V1_AFE_Policy.schema.json` |

Both blockers stay `MISSING`/`BLOCKED` until their own artifact is approved
and bound; providing one does not resolve the other.

## 5. Which fields are product decisions

Product decisions (approver-supplied) include, but are not limited to:

- NTC: exact device identity, source document revision, bias resistor and
  REGOUT values, table points, coverage endpoints, provenance, approvals.
- AFE: SYS_CTRL1/SYS_CTRL2/CELLBAL/CC_CFG approved values and readback
  rules, OV/UV/OCD/SCD physical targets and codes, Rsense nominal and
  polarity, calibration read-failure/invalid-decode behavior, XREADY
  recovery details, FET handoff/enable conditions, all `null`/`"<REQUIRED>"`
  policy fields.

## 6. Which fields are hardware-validation-only

These are deferred to hardware validation and are NOT satisfiable by a
filled template:

- actual NTC temperature accuracy, lot variation, full-range error;
- actual Rsense tolerance, Kelvin routing, copper resistance, current
  gain/offset and polarity waveforms;
- actual AFE OV/UV/OCD/SCD trip threshold/delay accuracy and ALERT/W1C
  physical commit behavior;
- actual FET/MOS turn-on/off timing and conduction;
- IWDG physical timing/reset, brownout, EMI/ESD, thermal, power integrity.

## 7. Reference values are not production approvals

Where a template contains a `current_software_reference` section, the values
there (e.g. 10000 Ω pull-up, 3.3 V REGOUT, SYS_CTRL1 `0x18`, SYS_CTRL2
`0x00`/`0x40`, CC_CFG `0x19`, `BMS_RSENSE_REFERENCE_UOHM` = 4000 µΩ,
`BMS_CURRENT_POLARITY` = +1) are current software/source constants only.
Every such section is marked **NOT APPROVED PRODUCTION VALUE**. Reference
values never satisfy an approved field; `BMS_RSENSE_REFERENCE_UOHM` does not
silently count as approval.

## 8. How final artifact files should be named

Recommended immutable names once approved (stored under the future
immutable approved-input path defined by the gate binding plan):

- `BMS_V1_NTC_Config_<revision>.json`
- `BMS_V1_AFE_Policy_<revision>.json`

`<revision>` is the exact immutable revision identifier recorded inside the
artifact. Do NOT create fake approved artifacts now.

## 9. Placeholders / null / DRAFT mean invalid for gate purposes

A populated artifact is NOT valid for gate purposes while any of the
following remains:

- `null` anywhere in a required field;
- a `"<REQUIRED>"` placeholder string;
- `"DRAFT"` (or any non-`"APPROVED"`) approval status.

The schemas enforce most of this structurally (null and placeholders do not
match the required types); the gate logic additionally checks every value
semantically.

## 10. Schemas validate structure, but semantic checks still require gate logic

The JSON Schemas validate structure: required sections, types, integer
ranges taken only from the C type/domain of the consuming source APIs, and
forbidden extra sections. They cannot prove semantics, for example:

- strict resistance monotonicity + opposite strict temperature
  monotonicity of the NTC table (JSON Schema cannot express this; the
  future gate/tooling must verify it);
- `point_count == points.length`;
- `minimum_temperature_decic < maximum_temperature_decic`;
- that physical targets recompute to the approved register codes;
- that approval evidence and hashes match external records.

Those checks belong to the future gate logic
(`BMS_V1_Phase8_Future_Gate_Binding_Plan.md`), which is a plan, not
implemented code.

---

Status of this package: **READY** (intake preparation only). It does NOT
resolve Blocker-1 or Blocker-2. Phase 8 Hard Gate remains `BLOCKED(2)`;
Phase 9 official implementation remains `NOT STARTED`.
