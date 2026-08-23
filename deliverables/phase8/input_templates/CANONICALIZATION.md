# BMS Canonical JSON v1

Algorithm ID: `BMS_CANONICAL_JSON_V1`

This algorithm defines the **canonical projection hash** used to identify an
approved Phase 8 blocker artifact. It does not define a hash of the complete
stored artifact bytes. Schema files, approval records, manifests, and generated
files use Git-blob and/or raw-file hashes instead.

## 1. Input decoding

Read the exact file bytes and require strict UTF-8. Reject a UTF-8 BOM, invalid
UTF-8, multiple JSON values, and trailing non-whitespace JSON content.

## 2. Strict parse

Reject duplicate object member names at every depth. Reject `NaN`, `Infinity`,
and `-Infinity`. Reject every numeric token containing `.`, `e`, or `E`, and
reject the lexical integer `-0`. Every integer must be in the interoperable
range `[-9007199254740991, 9007199254740991]`.

## 3. Unicode

Reject unpaired Unicode surrogates. Every object key and string value must
already be Unicode NFC. Do not silently normalize: reject a string when
`unicodedata.normalize("NFC", value) != value`.

## 4. Projection

Require a root object, an `approval` object, and exactly one
`approval.artifact_sha256` member. Deep-copy the parsed artifact and remove
only `/approval/artifact_sha256`.

Do not remove `approval.status`, `approval.approved_by`,
`approval.approved_at`, `approval.approval_record`, or any other contract
field. Array order is significant. Optional-field absence and presence are
different. Do not normalize line endings inside string values.

## 5. Serialization

Serialize the projection equivalently to:

```python
json.dumps(
    projection,
    sort_keys=True,
    separators=(",", ":"),
    ensure_ascii=False,
    allow_nan=False,
)
```

Encode as UTF-8 with no BOM, no trailing newline, and no insignificant
whitespace. Integers use the shortest base-10 form and zero is `0`. Keys sort
according to Python string ordering after surrogate and NFC rejection.

## 6. Hash

Compute SHA-256 over the canonical projection bytes and render lowercase
64-character hexadecimal. `approval.artifact_sha256` must match
`^[0-9a-f]{64}$` and equal the recomputed canonical projection hash.

Changing `approval.artifact_sha256` alone does not change the recomputed hash.
Changing any other contract field does change it.
