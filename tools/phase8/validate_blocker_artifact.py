#!/usr/bin/env python3
"""Offline Phase 8 blocker-artifact preflight validator.

This tool validates contracts and bindings only. It never changes the
production gate and never represents hardware validation.
"""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import hashlib
import json
import re
import subprocess
import sys
import unicodedata
from pathlib import Path
from typing import Any, Iterable


CANONICALIZATION_ID = "BMS_CANONICAL_JSON_V1"
MAX_INTEROPERABLE_INTEGER = 9007199254740991
REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_DIR = REPO_ROOT / "deliverables" / "phase8" / "input_templates"
SCHEMA_PATHS = {
    "ntc": SCHEMA_DIR / "BMS_V1_NTC_Config.v2.schema.json",
    "afe": SCHEMA_DIR / "BMS_V1_AFE_Policy.v2.schema.json",
    "approval": SCHEMA_DIR / "BMS_V1_Artifact_Approval_Record.v1.schema.json",
    "manifest": SCHEMA_DIR / "BMS_V1_Phase8_Gate_Manifest.v1.schema.json",
}

NTC_SCHEMA_ID = "https://bms-v1.local/schemas/v2/BMS_V1_NTC_Config.schema.json"
AFE_SCHEMA_ID = "https://bms-v1.local/schemas/v2/BMS_V1_AFE_Policy.schema.json"

EXPECTED_SCHEMA_BINDINGS = {
    "ntc": {
        "schema_id": NTC_SCHEMA_ID,
        "schema_version": 2,
        "schema_repository_path":
            "deliverables/phase8/input_templates/BMS_V1_NTC_Config.v2.schema.json",
    },
    "afe": {
        "schema_id": AFE_SCHEMA_ID,
        "schema_version": 2,
        "schema_repository_path":
            "deliverables/phase8/input_templates/BMS_V1_AFE_Policy.v2.schema.json",
    },
}

# 与 firmware/DRV/BQ76940/bq76940_control.c 镜像；regression 同时冻结 value 与源码引用。
OV_DELAY_S = (1, 2, 4, 8)
UV_DELAY_S = (1, 4, 8, 16)
OCD_DELAY_MS = (8, 20, 40, 80, 160, 320, 640, 1280)
SCD_DELAY_US = (70, 100, 200, 400)
OCD_THRESHOLD_MV = {
    1: (17, 22, 28, 33, 39, 44, 50, 56, 61, 67, 72, 78, 83, 89, 94, 100),
    0: (8, 11, 14, 17, 19, 22, 25, 28, 31, 33, 36, 39, 42, 44, 47, 50),
}
SCD_THRESHOLD_MV = {
    1: (44, 67, 89, 111, 133, 155, 178, 200),
    0: (22, 33, 44, 56, 67, 78, 89, 100),
}

RFC3339_UTC_RE = re.compile(
    r"^[0-9]{4}-(0[1-9]|1[0-2])-([0-2][0-9]|3[01])"
    r"T([01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9](\.[0-9]+)?Z$"
)
LOWER_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
PLACEHOLDER_WORDS = {"DRAFT", "TBD", "TODO", "PLACEHOLDER"}


class StrictJsonError(ValueError):
    """Input violates BMS_CANONICAL_JSON_V1 strict JSON rules."""


class ValidationFailure(ValueError):
    """Contract, semantic, or binding validation failed closed."""


class DependencyUnavailable(RuntimeError):
    """A required validation dependency is unavailable."""


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise StrictJsonError(f"duplicate object member name: {key!r}")
        result[key] = value
    return result


def _parse_int(token: str) -> int:
    if token == "-0":
        raise StrictJsonError("lexical integer -0 is forbidden")
    value = int(token, 10)
    if not -MAX_INTEROPERABLE_INTEGER <= value <= MAX_INTEROPERABLE_INTEGER:
        raise StrictJsonError(f"integer outside interoperable range: {token}")
    return value


def _reject_noninteger(token: str) -> Any:
    raise StrictJsonError(f"non-integer numeric token is forbidden: {token}")


def _reject_constant(token: str) -> Any:
    raise StrictJsonError(f"non-JSON numeric constant is forbidden: {token}")


def _decode_surrogate_pairs(value: str, path: str) -> str:
    result: list[str] = []
    index = 0
    while index < len(value):
        codepoint = ord(value[index])
        if 0xD800 <= codepoint <= 0xDBFF:
            if index + 1 >= len(value):
                raise StrictJsonError(f"unpaired Unicode surrogate at {path}")
            low = ord(value[index + 1])
            if not 0xDC00 <= low <= 0xDFFF:
                raise StrictJsonError(f"unpaired Unicode surrogate at {path}")
            scalar = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00)
            result.append(chr(scalar))
            index += 2
            continue
        if 0xDC00 <= codepoint <= 0xDFFF:
            raise StrictJsonError(f"unpaired Unicode surrogate at {path}")
        result.append(value[index])
        index += 1
    return "".join(result)


def _check_unicode(value: Any, path: str = "$") -> Any:
    if isinstance(value, str):
        scalar_value = _decode_surrogate_pairs(value, path)
        if unicodedata.normalize("NFC", scalar_value) != scalar_value:
            raise StrictJsonError(f"string is not Unicode NFC at {path}")
        return scalar_value
    elif isinstance(value, list):
        for index, item in enumerate(value):
            value[index] = _check_unicode(item, f"{path}[{index}]")
        return value
    elif isinstance(value, dict):
        result: dict[str, Any] = {}
        for key, item in value.items():
            checked_key = _check_unicode(key, f"{path}.<key>")
            if checked_key in result:
                raise StrictJsonError(f"duplicate object member name after Unicode decoding: {checked_key!r}")
            result[checked_key] = _check_unicode(item, f"{path}.{checked_key}")
        return result
    return value


def load_strict_json_bytes(raw: bytes) -> Any:
    """Parse exact bytes according to BMS_CANONICAL_JSON_V1."""
    if raw.startswith(b"\xef\xbb\xbf"):
        raise StrictJsonError("UTF-8 BOM is forbidden")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise StrictJsonError(f"invalid UTF-8: {error}") from error
    try:
        value = json.loads(
            text,
            object_pairs_hook=_reject_pairs,
            parse_int=_parse_int,
            parse_float=_reject_noninteger,
            parse_constant=_reject_constant,
        )
    except StrictJsonError:
        raise
    except json.JSONDecodeError as error:
        raise StrictJsonError(f"invalid or non-singular JSON: {error.msg}") from error
    return _check_unicode(value)


def load_strict_json(path: Path | str) -> Any:
    return load_strict_json_bytes(Path(path).read_bytes())


def canonical_projection_bytes(artifact: Any) -> bytes:
    if not isinstance(artifact, dict):
        raise ValidationFailure("canonical projection requires a root object")
    approval = artifact.get("approval")
    if not isinstance(approval, dict):
        raise ValidationFailure("canonical projection requires an approval object")
    if "artifact_sha256" not in approval:
        raise ValidationFailure("canonical projection requires exactly one approval.artifact_sha256")
    projection = copy.deepcopy(artifact)
    del projection["approval"]["artifact_sha256"]
    serialized = json.dumps(
        projection,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    )
    return serialized.encode("utf-8")


def canonical_projection_sha256(artifact: Any) -> str:
    return hashlib.sha256(canonical_projection_bytes(artifact)).hexdigest()


def raw_file_sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def git_blob_oid(raw: bytes, hex_length: int = 40) -> str:
    header = f"blob {len(raw)}\0".encode("ascii")
    if hex_length == 40:
        return hashlib.sha1(header + raw).hexdigest()
    if hex_length == 64:
        return hashlib.sha256(header + raw).hexdigest()
    raise ValueError("unsupported Git object ID length")


def _json_path(parts: Iterable[Any]) -> str:
    result = "$"
    for part in parts:
        result += f"[{part}]" if isinstance(part, int) else f".{part}"
    return result


def _schema_validate(value: Any, schema_kind: str) -> None:
    try:
        from jsonschema import Draft202012Validator
        from jsonschema.exceptions import SchemaError
    except ImportError as error:
        raise DependencyUnavailable("jsonschema is required for full validation") from error

    schema = load_strict_json(SCHEMA_PATHS[schema_kind])
    try:
        Draft202012Validator.check_schema(schema)
    except SchemaError as error:
        raise ValidationFailure(f"invalid {schema_kind} schema: {error.message}") from error
    errors = sorted(
        Draft202012Validator(schema).iter_errors(value),
        key=lambda item: tuple(str(part) for part in item.absolute_path),
    )
    if errors:
        details = [f"{_json_path(error.absolute_path)}: {error.message}" for error in errors]
        raise ValidationFailure(f"{schema_kind} schema validation failed:\n" + "\n".join(details))


def _check_placeholders(value: Any, path: str = "$") -> None:
    if isinstance(value, str):
        stripped = value.strip()
        if stripped.upper() in PLACEHOLDER_WORDS or stripped.upper().startswith("<REQUIRED"):
            raise ValidationFailure(f"placeholder value forbidden at {path}: {value!r}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _check_placeholders(item, f"{path}[{index}]")
    elif isinstance(value, dict):
        for key, item in value.items():
            _check_placeholders(item, f"{path}.{key}")


def _check_rfc3339_utc(value: str, path: str) -> None:
    if not isinstance(value, str) or RFC3339_UTC_RE.fullmatch(value) is None:
        raise ValidationFailure(f"strict RFC3339 UTC timestamp required at {path}")
    try:
        dt.datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as error:
        raise ValidationFailure(f"invalid calendar timestamp at {path}: {value}") from error


def _check_declared_hash(artifact: dict[str, Any]) -> str:
    declared = artifact["approval"]["artifact_sha256"]
    if not isinstance(declared, str) or LOWER_SHA256_RE.fullmatch(declared) is None:
        raise ValidationFailure("approval.artifact_sha256 must be lowercase SHA-256")
    computed = canonical_projection_sha256(artifact)
    if declared != computed:
        raise ValidationFailure(
            "approval.artifact_sha256 does not equal the canonical projection hash"
        )
    return computed


def _validate_ntc_semantics(artifact: dict[str, Any]) -> None:
    table = artifact["table"]
    points = table["points"]
    if table["point_count"] != len(points):
        raise ValidationFailure("NTC point_count does not equal len(points)")
    if len(points) < 2:
        raise ValidationFailure("NTC table requires at least two points")

    pairs = [(point["resistance_ohm"], point["temperature_decic"]) for point in points]
    if len(set(pairs)) != len(pairs):
        raise ValidationFailure("NTC duplicate points are forbidden")
    resistances = [pair[0] for pair in pairs]
    temperatures = [pair[1] for pair in pairs]
    if table["ordering"] == "resistance_ascending":
        resistance_ok = all(left < right for left, right in zip(resistances, resistances[1:]))
        temperature_ok = all(left > right for left, right in zip(temperatures, temperatures[1:]))
    else:
        resistance_ok = all(left > right for left, right in zip(resistances, resistances[1:]))
        temperature_ok = all(left < right for left, right in zip(temperatures, temperatures[1:]))
    if not resistance_ok:
        raise ValidationFailure("NTC resistance is not strictly monotonic in the declared ordering")
    if not temperature_ok:
        raise ValidationFailure("NTC temperature is not strictly monotonic in the opposite direction")

    coverage = artifact["coverage"]
    if coverage["minimum_temperature_decic"] >= coverage["maximum_temperature_decic"]:
        raise ValidationFailure("NTC minimum_temperature_decic must be less than maximum")
    if coverage["minimum_temperature_decic"] != min(temperatures):
        raise ValidationFailure("NTC minimum coverage does not match the table endpoint extreme")
    if coverage["maximum_temperature_decic"] != max(temperatures):
        raise ValidationFailure("NTC maximum coverage does not match the table endpoint extreme")


EXPECTED_INITIALIZER_BINDINGS = {
    ("/hardware_protection_policy", "ov_uv_trip_present", "constant_true_after_validation"),
    ("/hardware_protection_policy/ov/target_cell_mv", "ov_trip_mv", "identity_integer"),
    ("/hardware_protection_policy/uv/target_cell_mv", "uv_trip_mv", "identity_integer"),
    ("/hardware_protection_policy/scd", "protect1_present", "constant_true_after_validation"),
    ("/current_rsense_policy/rsns_bit", "protect1_rsns", "zero_false_one_true"),
    ("/hardware_protection_policy/scd/delay_code", "protect1_scd_delay_code", "identity_integer"),
    ("/hardware_protection_policy/scd/threshold_code", "protect1_scd_threshold_code", "identity_integer"),
    ("/hardware_protection_policy/ocd", "protect2_present", "constant_true_after_validation"),
    ("/hardware_protection_policy/ocd/delay_code", "protect2_ocd_delay_code", "identity_integer"),
    ("/hardware_protection_policy/ocd/threshold_code", "protect2_ocd_threshold_code", "identity_integer"),
    ("/hardware_protection_policy", "protect3_present", "constant_true_after_validation"),
    ("/hardware_protection_policy/uv/delay_code", "protect3_uv_delay_code", "identity_integer"),
    ("/hardware_protection_policy/ov/delay_code", "protect3_ov_delay_code", "identity_integer"),
}

EXPECTED_REGISTER_BINDINGS = {
    ("/startup_policy/sys_ctrl1/desired_byte", "BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED"),
    ("/startup_policy/sys_ctrl2_early/desired_byte", "BMS_AFE_STARTUP_SYS_CTRL2_FET_OFF"),
    ("/startup_policy/sys_ctrl2_final/desired_byte", "BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF"),
    ("/startup_policy/cellbal_startup/cellbal1_desired_byte", "CELLBAL1"),
    ("/startup_policy/cellbal_startup/cellbal2_desired_byte", "CELLBAL2"),
    ("/startup_policy/cellbal_startup/cellbal3_desired_byte", "CELLBAL3"),
    ("/startup_policy/cc_cfg/desired_byte", "BQ76940_CC_CFG_REQUIRED_VALUE"),
    ("/startup_policy/sys_stat/final_blocking_mask", "BMS_AFE_STARTUP_STAT_BLOCKING_MASK"),
}


def _requested_threshold_ratio(policy: dict[str, Any], rsense_uohm: int) -> tuple[int, int]:
    target = policy["target"]
    if target["basis"] == "sense_voltage_mv":
        return target["sense_voltage_mv"], 1
    return target["current_ma"] * rsense_uohm, 1_000_000


def _expected_not_below_code(table: tuple[int, ...], numerator: int, denominator: int) -> int:
    for code, threshold_mv in enumerate(table):
        if threshold_mv * denominator >= numerator:
            return code
    raise ValidationFailure("requested OCD/SCD threshold exceeds the selected RSNS table")


def _check_threshold_policy(
    name: str,
    policy: dict[str, Any],
    table: tuple[int, ...],
    rsense_uohm: int,
) -> None:
    code = policy["threshold_code"]
    if policy["selected_sense_voltage_mv"] != table[code]:
        raise ValidationFailure(f"{name} selected sense voltage does not match threshold code")
    numerator, denominator = _requested_threshold_ratio(policy, rsense_uohm)
    expected_code = _expected_not_below_code(table, numerator, denominator)
    if code != expected_code:
        raise ValidationFailure(f"{name} threshold code violates not-below-requested selection")


def _validate_afe_semantics(artifact: dict[str, Any]) -> None:
    if artifact["device_basis"]["policy_revision"] != artifact["revision"]:
        raise ValidationFailure("AFE device_basis.policy_revision must equal artifact revision")
    protection = artifact["hardware_protection_policy"]
    if protection["uv"]["target_cell_mv"] >= protection["ov"]["target_cell_mv"]:
        raise ValidationFailure("AFE UV target must be lower than OV target")

    delay_checks = (
        ("OV", protection["ov"], OV_DELAY_S),
        ("UV", protection["uv"], UV_DELAY_S),
        ("OCD", protection["ocd"], OCD_DELAY_MS),
        ("SCD", protection["scd"], SCD_DELAY_US),
    )
    for name, policy, table in delay_checks:
        if policy["hardware_delay"]["value"] != table[policy["delay_code"]]:
            raise ValidationFailure(f"{name} physical delay does not match source delay code")

    rsense = artifact["current_rsense_policy"]
    rsns_bit = rsense["rsns_bit"]
    _check_threshold_policy(
        "OCD", protection["ocd"], OCD_THRESHOLD_MV[rsns_bit], rsense["rsense_nominal_uohm"]
    )
    _check_threshold_policy(
        "SCD", protection["scd"], SCD_THRESHOLD_MV[rsns_bit], rsense["rsense_nominal_uohm"]
    )

    mapping = artifact["production_mapping"]
    initializer = {
        (item["artifact_pointer"], item["target_field"], item["transform"])
        for item in mapping["initializer_bindings"]
    }
    if initializer != EXPECTED_INITIALIZER_BINDINGS:
        raise ValidationFailure("AFE BMS_AfeStartupConfig_t initializer mapping is incomplete or altered")
    register = {
        (item["artifact_pointer"], item["production_symbol_or_register"])
        for item in mapping["fixed_register_policy_bindings"]
    }
    if register != EXPECTED_REGISTER_BINDINGS:
        raise ValidationFailure("AFE fixed startup register mapping is incomplete or altered")


def validate_artifact_object(artifact: Any, kind: str) -> str:
    if kind not in ("ntc", "afe"):
        raise ValueError(f"unknown artifact kind: {kind}")
    if not isinstance(artifact, dict):
        raise ValidationFailure("artifact root must be an object")
    _check_placeholders(artifact)
    _schema_validate(artifact, kind)
    _check_rfc3339_utc(artifact["approval"]["approved_at"], "$.approval.approved_at")
    computed_hash = _check_declared_hash(artifact)
    if kind == "ntc":
        _validate_ntc_semantics(artifact)
    else:
        _validate_afe_semantics(artifact)
    return computed_hash


def validate_artifact_file(path: Path | str, kind: str) -> tuple[dict[str, Any], str, bytes]:
    raw = Path(path).read_bytes()
    artifact = load_strict_json_bytes(raw)
    computed_hash = validate_artifact_object(artifact, kind)
    return artifact, computed_hash, raw


def validate_pair(ntc: dict[str, Any], afe: dict[str, Any]) -> None:
    if ntc["hardware_identity"] != afe["hardware_identity"]:
        raise ValidationFailure("NTC/AFE hardware_identity tuples do not match exactly")
    ntc_basis = ntc["ts_conversion_basis"]["bq_datasheet_revision"]
    afe_basis = afe["device_basis"]["datasheet_revision"]
    if ntc_basis != afe_basis:
        raise ValidationFailure("NTC/AFE BQ datasheet revisions do not match")


def _approval_kind(artifact: dict[str, Any]) -> str:
    artifact_type = artifact.get("artifact_type")
    if artifact_type == "BMS_V1_NTC_CONFIG":
        return "ntc"
    if artifact_type == "BMS_V1_AFE_STARTUP_PROTECTION_POLICY":
        return "afe"
    raise ValidationFailure(f"unsupported artifact_type for approval binding: {artifact_type!r}")


def _path_matches(supplied_path: Path | str, repository_path: str) -> bool:
    supplied = Path(supplied_path).resolve().as_posix()
    declared = repository_path.replace("\\", "/").lstrip("./")
    return supplied == declared or supplied.endswith("/" + declared)


def validate_approval_record_object(record: Any) -> None:
    if not isinstance(record, dict):
        raise ValidationFailure("approval record root must be an object")
    _check_placeholders(record)
    _schema_validate(record, "approval")
    _check_rfc3339_utc(record["approver"]["approved_at"], "$.approver.approved_at")


def validate_approval_binding(
    artifact: dict[str, Any],
    record: dict[str, Any],
    *,
    artifact_path: Path | str | None = None,
    artifact_raw: bytes | None = None,
) -> str:
    kind = _approval_kind(artifact)
    computed_hash = validate_artifact_object(artifact, kind)
    validate_approval_record_object(record)
    binding = record["artifact"]
    expected = {
        "artifact_type": artifact["artifact_type"],
        "artifact_schema_version": artifact["schema_version"],
        "artifact_schema_id": artifact["schema_id"],
        "artifact_revision": artifact["revision"],
        "artifact_canonicalization_id": CANONICALIZATION_ID,
        "artifact_canonical_projection_sha256": computed_hash,
    }
    for field, expected_value in expected.items():
        if binding[field] != expected_value:
            raise ValidationFailure(f"detached approval record mismatch: artifact.{field}")
    if record["hardware_identity"] != artifact["hardware_identity"]:
        raise ValidationFailure("detached approval record hardware_identity mismatch")
    if artifact["approval"]["approval_record"] != record["record_id"]:
        raise ValidationFailure("artifact approval_record declaration does not match record_id")
    if artifact["approval"]["approved_by"] != record["approver"]["identity"]:
        raise ValidationFailure("artifact approved_by declaration does not match detached approver")
    if artifact["approval"]["approved_at"] != record["approver"]["approved_at"]:
        raise ValidationFailure("artifact approved_at declaration does not match detached approval time")
    if artifact_path is not None and not _path_matches(artifact_path, binding["artifact_repository_path"]):
        raise ValidationFailure("detached approval artifact_repository_path does not match supplied file")
    if artifact_raw is not None:
        oid = binding["artifact_git_blob_sha"]
        if git_blob_oid(artifact_raw, len(oid)) != oid:
            raise ValidationFailure("detached approval artifact_git_blob_sha does not match supplied file")
    return computed_hash


def _safe_repository_file(repository_path: str) -> Path:
    candidate = (REPO_ROOT / Path(repository_path)).resolve()
    try:
        candidate.relative_to(REPO_ROOT)
    except ValueError as error:
        raise ValidationFailure(f"repository path escapes repository root: {repository_path}") from error
    if not candidate.is_file():
        raise ValidationFailure(f"bound repository file does not exist: {repository_path}")
    return candidate


def _run_git(repository_root: Path, arguments: list[str]) -> bytes:
    try:
        completed = subprocess.run(
            ["git", "-C", str(repository_root), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as error:
        raise ValidationFailure(f"local Git invocation failed: {error}") from error
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", "replace").strip()
        raise ValidationFailure(
            f"local Git verification failed for {' '.join(arguments)}: {detail}"
        )
    return completed.stdout


def _normalize_repository_identity(value: str) -> str:
    normalized = value.strip().replace("\\", "/")
    while normalized.endswith("/"):
        normalized = normalized[:-1]
    if normalized.lower().endswith(".git"):
        normalized = normalized[:-4]
    return normalized


def _resolve_commit(repository_root: Path, revision: str) -> str:
    resolved = _run_git(
        repository_root,
        ["rev-parse", "--verify", f"{revision}^{{commit}}"],
    ).decode("ascii", "strict").strip()
    if len(resolved) not in (40, 64):
        raise ValidationFailure(f"Git did not resolve a full commit ID: {revision}")
    return resolved


def _git_blob_at_commit(
    repository_root: Path,
    commit: str,
    repository_path: str,
) -> tuple[str, bytes]:
    object_spec = f"{commit}:{repository_path}"
    oid = _run_git(
        repository_root,
        ["rev-parse", "--verify", object_spec],
    ).decode("ascii", "strict").strip()
    object_type = _run_git(
        repository_root,
        ["cat-file", "-t", oid],
    ).decode("ascii", "strict").strip()
    if object_type != "blob":
        raise ValidationFailure(
            f"bound Git object is not a file blob: {repository_path}@{commit}"
        )
    return oid, _run_git(repository_root, ["cat-file", "blob", oid])


def _validate_repository_candidate(
    manifest: dict[str, Any],
    repository_root: Path,
) -> str:
    declared = manifest["candidate"]
    actual_repository = _run_git(
        repository_root,
        ["config", "--get", "remote.origin.url"],
    ).decode("utf-8", "strict").strip()
    if _normalize_repository_identity(declared["repository"]) != \
            _normalize_repository_identity(actual_repository):
        raise ValidationFailure("manifest candidate repository identity mismatch")

    candidate_commit = _resolve_commit(
        repository_root,
        declared["production_git_commit"],
    )
    if candidate_commit != declared["production_git_commit"]:
        raise ValidationFailure("manifest production_git_commit is not a full resolved commit ID")
    reference_commit = _resolve_commit(
        repository_root,
        declared["branch_or_reference"],
    )
    if reference_commit != candidate_commit:
        raise ValidationFailure("manifest branch_or_reference does not resolve to production_git_commit")
    return candidate_commit


def _validate_schema_binding(
    name: str,
    binding: dict[str, Any],
    repository_root: Path,
) -> None:
    expected = EXPECTED_SCHEMA_BINDINGS[name]
    for field, expected_value in expected.items():
        if binding[field] != expected_value:
            raise ValidationFailure(f"manifest {name} schema {field} is not the expected v2 contract")

    schema_file = _safe_repository_file_from_root(
        repository_root,
        binding["schema_repository_path"],
    )
    schema_raw = schema_file.read_bytes()
    if binding["schema_sha256"] != raw_file_sha256(schema_raw):
        raise ValidationFailure(f"manifest {name} schema raw-file SHA-256 mismatch")

    schema_commit = _resolve_commit(repository_root, binding["schema_git_commit"])
    committed_oid, committed_raw = _git_blob_at_commit(
        repository_root,
        schema_commit,
        binding["schema_repository_path"],
    )
    if binding["schema_git_blob_sha"] != committed_oid:
        raise ValidationFailure(f"manifest {name} schema declared Git blob mismatch")
    if committed_raw != schema_raw:
        raise ValidationFailure(f"manifest {name} schema working file differs from declared commit blob")


def _safe_repository_file_from_root(repository_root: Path, repository_path: str) -> Path:
    root = repository_root.resolve()
    candidate = (root / Path(repository_path)).resolve()
    try:
        candidate.relative_to(root)
    except ValueError as error:
        raise ValidationFailure(f"repository path escapes repository root: {repository_path}") from error
    if not candidate.is_file():
        raise ValidationFailure(f"bound repository file does not exist: {repository_path}")
    return candidate


def _validate_generated_binding(
    name: str,
    binding: dict[str, Any],
    repository_root: Path,
    candidate_commit: str,
) -> None:
    generated_file = _safe_repository_file_from_root(repository_root, binding["path"])
    generated_raw = generated_file.read_bytes()
    if binding["raw_file_sha256"] != raw_file_sha256(generated_raw):
        raise ValidationFailure(f"manifest {name} generated-output raw-file SHA-256 mismatch")
    committed_oid, committed_raw = _git_blob_at_commit(
        repository_root,
        candidate_commit,
        binding["path"],
    )
    if binding["git_blob_sha"] != committed_oid:
        raise ValidationFailure(f"manifest {name} generated-output declared Git blob mismatch")
    if committed_raw != generated_raw:
        raise ValidationFailure(f"manifest {name} generated output differs from candidate blob")


def _validate_evidence_binding(
    name: str,
    binding: dict[str, Any],
    repository_root: Path,
) -> None:
    evidence_commit = _resolve_commit(repository_root, binding["git_commit"])
    committed_oid, committed_raw = _git_blob_at_commit(
        repository_root,
        evidence_commit,
        binding["path"],
    )
    if binding["git_blob_sha"] != committed_oid:
        raise ValidationFailure(f"manifest {name} evidence declared Git blob mismatch")
    if binding["raw_file_sha256"] != raw_file_sha256(committed_raw):
        raise ValidationFailure(f"manifest {name} evidence raw-file SHA-256 mismatch")


def validate_manifest_object(
    manifest: dict[str, Any],
    ntc: dict[str, Any],
    ntc_record: dict[str, Any],
    afe: dict[str, Any],
    afe_record: dict[str, Any],
    *,
    paths: dict[str, Path | str] | None = None,
    raw_files: dict[str, bytes] | None = None,
    repository_root: Path = REPO_ROOT,
) -> None:
    _check_placeholders(manifest)
    _schema_validate(manifest, "manifest")
    ntc_hash = validate_approval_binding(
        ntc,
        ntc_record,
        artifact_path=None if paths is None else paths["ntc"],
        artifact_raw=None if raw_files is None else raw_files["ntc"],
    )
    afe_hash = validate_approval_binding(
        afe,
        afe_record,
        artifact_path=None if paths is None else paths["afe"],
        artifact_raw=None if raw_files is None else raw_files["afe"],
    )
    validate_pair(ntc, afe)
    if manifest["hardware_identity"] != ntc["hardware_identity"]:
        raise ValidationFailure("manifest hardware_identity does not match supplied artifacts")
    candidate_commit = _validate_repository_candidate(manifest, repository_root)

    supplied = {
        "ntc": (ntc, ntc_record, ntc_hash),
        "afe": (afe, afe_record, afe_hash),
    }
    for name, (artifact, record, computed_hash) in supplied.items():
        artifact_binding = manifest["artifacts"][name]
        if artifact_binding["revision"] != artifact["revision"]:
            raise ValidationFailure(f"manifest {name} artifact revision is stale")
        if artifact_binding["canonical_projection_sha256"] != computed_hash:
            raise ValidationFailure(f"manifest {name} canonical projection hash is stale")
        if artifact_binding["path"] != record["artifact"]["artifact_repository_path"]:
            raise ValidationFailure(f"manifest {name} artifact path disagrees with approval record")
        if raw_files is not None:
            oid = artifact_binding["git_blob_sha"]
            if git_blob_oid(raw_files[name], len(oid)) != oid:
                raise ValidationFailure(f"manifest {name} artifact Git blob identity mismatch")
        if paths is not None and not _path_matches(paths[name], artifact_binding["path"]):
            raise ValidationFailure(f"manifest {name} artifact path does not match supplied file")

        approval_binding = manifest["approval_records"][name]
        if approval_binding["record_id"] != record["record_id"]:
            raise ValidationFailure(f"manifest {name} approval record ID mismatch")
        if paths is not None and not _path_matches(paths[f"{name}_approval"], approval_binding["path"]):
            raise ValidationFailure(f"manifest {name} approval path does not match supplied file")
        if raw_files is not None:
            approval_raw = raw_files[f"{name}_approval"]
            if approval_binding["raw_file_sha256"] != raw_file_sha256(approval_raw):
                raise ValidationFailure(f"manifest {name} approval raw-file SHA-256 mismatch")
            oid = approval_binding["git_blob_sha"]
            if git_blob_oid(approval_raw, len(oid)) != oid:
                raise ValidationFailure(f"manifest {name} approval Git blob identity mismatch")

        generated = manifest["generated_outputs"][name]
        if generated["source_artifact_revision"] != artifact["revision"]:
            raise ValidationFailure(f"manifest {name} generated-output artifact revision mismatch")
        if generated["source_artifact_canonical_projection_sha256"] != computed_hash:
            raise ValidationFailure(f"manifest {name} generated-output artifact hash mismatch")
        if generated["production_git_commit"] != candidate_commit:
            raise ValidationFailure(f"manifest {name} generated output binds a different candidate")
        _validate_generated_binding(name, generated, repository_root, candidate_commit)

        schema_binding = manifest["schemas"][name]
        if schema_binding["schema_id"] != artifact["schema_id"]:
            raise ValidationFailure(f"manifest {name} schema ID mismatch")
        if schema_binding["schema_version"] != artifact["schema_version"]:
            raise ValidationFailure(f"manifest {name} schema version mismatch")
        _validate_schema_binding(name, schema_binding, repository_root)

    if manifest["evidence"]["production_git_commit"] != candidate_commit:
        raise ValidationFailure("manifest evidence binds a different production candidate")
    for name in ("build", "test", "verifier", "manifest"):
        _validate_evidence_binding(
            name,
            manifest["evidence"][name],
            repository_root,
        )


def validate_manifest_files(
    manifest_path: Path | str,
    ntc_path: Path | str,
    ntc_approval_path: Path | str,
    afe_path: Path | str,
    afe_approval_path: Path | str,
) -> None:
    paths = {
        "manifest": manifest_path,
        "ntc": ntc_path,
        "ntc_approval": ntc_approval_path,
        "afe": afe_path,
        "afe_approval": afe_approval_path,
    }
    raw_files = {name: Path(path).read_bytes() for name, path in paths.items()}
    manifest = load_strict_json_bytes(raw_files["manifest"])
    ntc = load_strict_json_bytes(raw_files["ntc"])
    ntc_record = load_strict_json_bytes(raw_files["ntc_approval"])
    afe = load_strict_json_bytes(raw_files["afe"])
    afe_record = load_strict_json_bytes(raw_files["afe_approval"])
    validate_manifest_object(
        manifest,
        ntc,
        ntc_record,
        afe,
        afe_record,
        paths=paths,
        raw_files=raw_files,
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ntc", type=Path, help="validate an NTC v2 artifact")
    parser.add_argument("--afe", type=Path, help="validate an AFE v2 artifact")
    parser.add_argument("--pair", action="store_true", help="validate NTC/AFE compatibility")
    parser.add_argument("--print-hash", type=Path, metavar="FILE", help="print canonical projection hash only")
    parser.add_argument("--artifact", type=Path, help="artifact for detached approval binding")
    parser.add_argument("--approval-record", type=Path, help="detached approval record")
    parser.add_argument("--manifest", type=Path, help="full candidate-binding manifest")
    parser.add_argument("--ntc-approval", type=Path, help="NTC detached approval record")
    parser.add_argument("--afe-approval", type=Path, help="AFE detached approval record")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        if args.print_hash is not None:
            artifact = load_strict_json(args.print_hash)
            result = canonical_projection_sha256(artifact)
            print(f"HASH ONLY — NOT APPROVAL: {result}")
            return 0

        if args.manifest is not None:
            required = (args.ntc, args.ntc_approval, args.afe, args.afe_approval)
            if any(item is None for item in required):
                raise OSError("--manifest requires --ntc, --ntc-approval, --afe, and --afe-approval")
            validate_manifest_files(
                args.manifest, args.ntc, args.ntc_approval, args.afe, args.afe_approval
            )
            print("MANIFEST PREFLIGHT PASS — NOT A PHASE8 HARD GATE RESULT")
            return 0

        if args.artifact is not None or args.approval_record is not None:
            if args.artifact is None or args.approval_record is None:
                raise OSError("--artifact and --approval-record must be supplied together")
            artifact_raw = args.artifact.read_bytes()
            artifact = load_strict_json_bytes(artifact_raw)
            record = load_strict_json(args.approval_record)
            validate_approval_binding(
                artifact,
                record,
                artifact_path=args.artifact,
                artifact_raw=artifact_raw,
            )
            print("APPROVAL RECORD BINDING PASS — NOT A PHASE8 HARD GATE RESULT")
            return 0

        if args.pair and (args.ntc is None or args.afe is None):
            raise OSError("--pair requires both --ntc and --afe")
        if args.ntc is None and args.afe is None:
            raise OSError("no validation operation selected")

        ntc = afe = None
        if args.ntc is not None:
            ntc, _, _ = validate_artifact_file(args.ntc, "ntc")
        if args.afe is not None:
            afe, _, _ = validate_artifact_file(args.afe, "afe")
        if args.pair:
            assert ntc is not None and afe is not None
            validate_pair(ntc, afe)
        print("PREFLIGHT VALIDATION PASS — NOT A PHASE8 HARD GATE RESULT")
        print("Artifact declaration verified internally; external approval not verified.")
        return 0
    except DependencyUnavailable as error:
        print(f"DEPENDENCY UNAVAILABLE: {error}", file=sys.stderr)
        return 3
    except (OSError, IOError) as error:
        print(f"INVOCATION/FILE ERROR: {error}", file=sys.stderr)
        return 2
    except (StrictJsonError, ValidationFailure) as error:
        print(f"PREFLIGHT VALIDATION FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
