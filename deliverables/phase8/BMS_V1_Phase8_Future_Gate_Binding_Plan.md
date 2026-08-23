# Phase 8 Future Gate Binding Plan — Artifact Contract v2

Status: future gate plan only. The offline v2 validator is implemented, but the
production Phase 8 verifier and firmware wiring are unchanged. A preflight
pass is not a Phase 8 Hard Gate pass and hardware validation remains separate.

The historical v1 artifact contract is **SUPERSEDED — DO NOT USE FOR FUTURE
GATE**. The v2 schemas, detached approval record, and gate manifest are the
current candidate trust chain.

## Trust chain

```text
approved v2 artifact
  -> BMS_CANONICAL_JSON_V1 canonical projection SHA-256
  -> detached approval record (independent authority)
  -> generated semantic output
  -> production wiring at one Git candidate
  -> build/test/verifier evidence
  -> candidate-binding gate manifest
```

The artifact canonical projection hash excludes only
`/approval/artifact_sha256`. Schema, approval-record, generated-file, and
evidence identity uses raw-file SHA-256 and/or Git blob identity. These roles
must never be conflated.

## Future gate stages

1. Strict-parse both artifacts, both detached approval records, and the
   manifest. Reject BOM, invalid UTF-8, duplicates, non-integer numeric tokens,
   negative zero, interoperable-range violations, non-NFC strings, unpaired
   surrogates, placeholders, or trailing JSON content.
2. Validate all four contracts with Draft 2020-12 JSON Schema and the semantic
   rules in `tools/phase8/validate_blocker_artifact.py`.
3. Recompute each artifact canonical projection SHA-256. Require lowercase
   declarations and exact detached approval record bindings.
4. Require exact NTC/AFE common hardware identity and matching applicable BQ
   datasheet revision.
5. Validate the manifest against the supplied files: schema identities,
   artifact revisions/hashes/blobs, approval records, generated outputs,
   evidence, and one production Git candidate.
6. Generate or ingest candidate C output, compare semantic values to the
   artifacts, and verify production consumption at the manifest candidate.
7. Run the production Keil build, regression tests, and the future revised
   Phase 8 verifier at the exact candidate revision. Bind their evidence to the
   manifest.
8. Only the future integrated gate may evaluate whether both blockers are
   resolved. Offline validation never prints or implies that result.

## NTC semantic consumption

The future gate must prove:

```text
NTC v2 table
  -> exact ordered (resistance_ohm, temperature_decic) values and count
  -> BMS_NtcPoint_t generated/source initializer
  -> immutable table lifetime
  -> BMS_Sample_SetNtcTable production call
```

It must also prove:

```text
ts_conversion_basis
  -> bias resistor nominal/tolerance basis
  -> REGOUT and TS ADC LSB production constants
  -> TS1 resistance conversion path
```

Compare semantic integer fields and count. Do not compare raw C memory, struct
padding, compiler layout, byte order, or a raw-struct hash.

## AFE semantic consumption

The future gate must use `production_mapping` to prove:

```text
AFE v2 OV/UV/OCD/SCD + single Rsense policy
  -> every BMS_AfeStartupConfig_t initializer field
  -> BQ76940 control encoding and source-native delay tables
  -> BMS_AfeStartup_Init production invocation
  -> BMS_AfeStartup_Step runtime execution
```

Fixed startup policy must be compared against SYS_CTRL1, SYS_CTRL2 early/final,
CELLBAL1/2/3, CC_CFG, SYS_STAT, initial-settle, and CC_READY source behavior.
OCD/SCD threshold verification consumes the one `rsns_bit` and uses the same
not-below-requested rule as `firmware/Driver/bq76940_control.c`. Current-basis
targets use exact integer/rational arithmetic; no floating point is permitted.

The future gate must additionally prove:

```text
approved Rsense + polarity
  -> BQ76940 current conversion production path

runtime device ADCGAIN1/ADCOFFSET/ADCGAIN2 reads
  -> valid decoded calibration + generation/provenance checks
  -> RecoveryCoordinator-owned BMS_Sample_SetCalibration handoff
  -> no static/default fallback
```

## Phase 8 / Phase 9 boundary

Blocker-2 remains limited to AFE startup/protection, Rsense/current mapping,
AFE hardware OV/UV/OCD/SCD, runtime calibration handoff, XREADY technical
recovery, and startup/FET-safe handoff required by the frozen architecture.

The Phase 8 artifact must not resolve Phase 9 OP-01 through OP-08. Until future
policy exists, the frozen fallback remains action-bearing and fail-closed:
recovery-in-progress plus historical XREADY, SCD, and OVRD_ALERT latches
inhibit both FET directions. No artifact field may select `ALLOW` or generic
latch clear.

## Detached approval and manifest limits

Artifact `approval` fields are declarations. External approval is verified
only when the independent record matches every binding. Reviewed immutable Git
evidence is sufficient; PKI is not required by this contract.

The manifest binds a candidate but does not approve it, run Keil, inspect
production calls, execute the Phase 8 verifier, or provide hardware evidence.
Generated/evidence structural presence is necessary but insufficient; future
gate execution must verify the referenced bytes and behavior.

## Hardware-validation boundary

The future software gate must not represent simulator, build, schema, or static
mapping evidence as proof of physical NTC accuracy, Rsense tolerance/Kelvin
routing, current polarity waveforms, OV/UV/OCD/SCD physical trip accuracy,
ALERT/W1C commit behavior, MOS conduction, or environmental robustness.

Until both approved artifacts, detached records, semantic production bindings,
and integrated gate evidence exist, Blocker-1 and Blocker-2 remain blocked,
Phase 8 Hard Gate remains `BLOCKED(2)`, and Phase 9 official implementation
remains `NOT STARTED`.
