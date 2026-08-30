# Phase 8 Blocker Artifact Contract v2

This directory contains the historical intake contract for the two Phase 8
policy inputs. Its offline **preflight** tooling and later approved bindings are
retained as part of the final evidence chain.

## Contract status

| Contract | Status | Use |
|---|---|---|
| NTC/AFE schema and template v1 | **SUPERSEDED — DO NOT USE FOR FUTURE GATE** | Historical Git evidence only |
| NTC/AFE schema and template v2 | Current candidate contract | Future blocker intake and preflight |
| Detached approval record v1 | Current candidate contract | Independent approval authority |
| Phase 8 gate manifest v1 | Current candidate contract | Bind one production candidate |

The four original v1 JSON files remain byte-preserved at commit `525dfa0`.
They are not redefined by v2.

## Files

- `BMS_V1_NTC_Config.v2.template.json` and `.schema.json`: Blocker-1
  approved-artifact contract.
- `BMS_V1_AFE_Policy.v2.template.json` and `.schema.json`: Blocker-2
  approved-artifact contract.
- `BMS_V1_Artifact_Approval_Record.v1.template.json` and `.schema.json`:
  detached approval evidence.
- `BMS_V1_Phase8_Gate_Manifest.v1.template.json` and `.schema.json`:
  candidate binding; not approval.
- `CANONICALIZATION.md`: frozen `BMS_CANONICAL_JSON_V1` definition.
- `tools/phase8/validate_blocker_artifact.py`: offline preflight validator.

Every template is deliberately invalid against its final schema. `null`,
`<REQUIRED...>`, and `DRAFT` values must be replaced only with reviewed
hardware/product evidence. Do not insert guessed NTC, Rsense, OV/UV/OCD/SCD,
or other production values.

## Artifact identity and repository identity

An NTC or AFE artifact is identified by its **canonical projection SHA-256**.
`BMS_CANONICAL_JSON_V1` excludes only
`/approval/artifact_sha256`; all other fields remain hash-covered. The hash is
not the SHA-256 of the complete stored file bytes.

Approval records, schemas, generated files, and evidence files use Git-blob
and/or raw-file SHA-256 identity. These identities have different roles and
must not be substituted for one another.

## Approval authority

The artifact's `approval` object is a declaration. The artifact cannot
self-authorize. External approval is established only when an independent
`BMS_V1_ARTIFACT_APPROVAL_RECORD` matches the artifact type, schema, revision,
canonicalization ID, canonical projection hash, repository identity, hardware
identity, record ID, approver, and approval time.

The gate manifest is also not approval. It binds the two artifacts, two
detached approval records, exact schemas, generated outputs, evidence, and one
production Git candidate.

## Strict approved-artifact rules

- UTF-8 without BOM; no duplicate keys at any depth.
- Integer-only numeric tokens; no fractions, exponents, negative zero, or
  non-JSON numeric constants.
- Integers stay within the interoperable ±(2^53−1) range before tighter schema
  domains apply.
- Strings and keys are already Unicode NFC and contain no unpaired surrogate.
- No placeholders; approval status is exactly `APPROVED`; hashes are lowercase.
- Approval timestamps are strict RFC3339 UTC ending in `Z`.
- Final artifacts contain no `current_software_reference` or other
  reference-only section. Source constants remain documentation/evidence only.

## AFE phase boundary

The Phase 8 AFE artifact covers startup/protection input, Rsense/current
mapping, AFE hardware OV/UV/OCD/SCD, runtime calibration handoff, XREADY
technical recovery, and startup/FET-safe handoff required by the frozen
architecture.

It does **not** resolve Phase 9 product-policy items OP-01 through OP-08. The
frozen fail-closed fallback remains machine-enforced: recovery in progress and
the XREADY/SCD/OVRD_ALERT action-bearing historical latches inhibit both CHG
and DSG until future policy. The product artifact cannot select `ALLOW`.

## Offline use

```text
python tools/phase8/validate_blocker_artifact.py --ntc <artifact.json>
python tools/phase8/validate_blocker_artifact.py --afe <artifact.json>
python tools/phase8/validate_blocker_artifact.py --pair --ntc <ntc.json> --afe <afe.json>
python tools/phase8/validate_blocker_artifact.py --print-hash <artifact.json>
python tools/phase8/validate_blocker_artifact.py --artifact <artifact.json> --approval-record <record.json>
python tools/phase8/validate_blocker_artifact.py --manifest <manifest.json> --ntc <ntc.json> --ntc-approval <record.json> --afe <afe.json> --afe-approval <record.json>
```

Full schema validation requires Python `jsonschema`. Missing dependency is
reported explicitly; it is never treated as a pass.

Preflight validates the input contract; the Keil build, production wiring,
Phase 8 verifier, and interface evidence are separate layers in the final
verification chain. Generated NTC and AFE C outputs are compared by semantic field
values and counts, never raw C struct bytes, padding, or endianness.

The two input identities and their approved production bindings are traceable
through the final Release Baseline.
