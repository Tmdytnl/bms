#!/usr/bin/env python3
"""Regression tests for Phase 8 artifact-contract v2 preflight tooling.

Every policy value in this module is SYNTHETIC TEST ONLY — NOT BMS POLICY.
No fixture is an approved production input.
"""

from __future__ import annotations

import contextlib
import copy
import hashlib
import io
import json
import re
import unittest
from unittest import mock
from pathlib import Path

import validate_blocker_artifact as validator


SYNTHETIC = "SYNTHETIC TEST ONLY - NOT BMS POLICY"
ZERO_HASH = "0" * 64
ZERO_OID = "0" * 40
TEST_COMMIT = "a" * 40


def seal(artifact: dict) -> dict:
    artifact["approval"]["artifact_sha256"] = ZERO_HASH
    artifact["approval"]["artifact_sha256"] = validator.canonical_projection_sha256(artifact)
    return artifact


def make_ntc() -> dict:
    """Return a valid synthetic artifact; all numeric values are test-only."""
    artifact = {
        "artifact_type": "BMS_V1_NTC_CONFIG",
        "schema_version": 2,
        "schema_id": validator.NTC_SCHEMA_ID,
        "revision": "synthetic-ntc-r1",
        "hardware_identity": {
            "project_id": "BMS_V1",
            "hardware_variant": "SYNTHETIC_TEST_HW",
            "board_revision": "SYNTHETIC_TEST_BOARD_R1",
            "schematic_revision": "SYNTHETIC_TEST_SCHEMATIC_R1",
            "afe_part": "BQ7694003_SYNTHETIC_TEST_VARIANT",
        },
        "device": {
            "manufacturer": SYNTHETIC,
            "exact_part": "SYNTHETIC_NTC_PART",
            "variant": "SYNTHETIC_NTC_VARIANT",
            "ntc_tolerance_curve_basis": SYNTHETIC,
        },
        "source_document": {
            "title": SYNTHETIC,
            "revision": "SYNTHETIC_SOURCE_R1",
            "source_location": SYNTHETIC,
            "source_type": SYNTHETIC,
        },
        "ts_conversion_basis": {
            "bq_datasheet_revision": "SYNTHETIC_BQ_DS_R1",
            "ts_channel": "TS1",
            "bias_resistor_nominal_ohm": 10000,
            "bias_resistor_tolerance_ppm": 10000,
            "regout_nominal_uv": 3300000,
            "ts_adc_lsb_uv": 382,
            "schematic_net_reference": SYNTHETIC,
            "bom_reference": SYNTHETIC,
            "model_confirmed": True,
        },
        "table": {
            "resistance_unit": "ohm",
            "temperature_unit": "deci_C",
            "ordering": "resistance_ascending",
            "point_count": 3,
            "points": [
                {"resistance_ohm": 1000, "temperature_decic": 1000},
                {"resistance_ohm": 2000, "temperature_decic": 0},
                {"resistance_ohm": 3000, "temperature_decic": -1000},
            ],
        },
        "coverage": {
            "minimum_temperature_decic": -1000,
            "maximum_temperature_decic": 1000,
            "out_of_range_behavior": "reject_no_extrapolation",
        },
        "provenance": {
            "source_curve_identifier": SYNTHETIC,
            "generation_method": SYNTHETIC,
            "generation_tool_or_record": SYNTHETIC,
        },
        "approval": {
            "status": "APPROVED",
            "approved_by": "SYNTHETIC_TEST_APPROVER",
            "approved_at": "2026-01-02T03:04:05Z",
            "approval_record": "synthetic-ntc-approval-r1",
            "artifact_sha256": ZERO_HASH,
        },
    }
    return seal(artifact)


def _initializer_bindings() -> list[dict]:
    values = [
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
    ]
    return [
        {"artifact_pointer": pointer, "target_field": field, "transform": transform}
        for pointer, field, transform in values
    ]


def _register_bindings() -> list[dict]:
    values = [
        ("/startup_policy/sys_ctrl1/desired_byte", "BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED"),
        ("/startup_policy/sys_ctrl2_early/desired_byte", "BMS_AFE_STARTUP_SYS_CTRL2_FET_OFF"),
        ("/startup_policy/sys_ctrl2_final/desired_byte", "BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF"),
        ("/startup_policy/cellbal_startup/cellbal1_desired_byte", "CELLBAL1"),
        ("/startup_policy/cellbal_startup/cellbal2_desired_byte", "CELLBAL2"),
        ("/startup_policy/cellbal_startup/cellbal3_desired_byte", "CELLBAL3"),
        ("/startup_policy/cc_cfg/desired_byte", "BQ76940_CC_CFG_REQUIRED_VALUE"),
        ("/startup_policy/sys_stat/final_blocking_mask", "BMS_AFE_STARTUP_STAT_BLOCKING_MASK"),
    ]
    return [
        {
            "artifact_pointer": pointer,
            "production_symbol_or_register": symbol,
            "comparison": "INTEGER_SEMANTIC_VALUE",
        }
        for pointer, symbol in values
    ]


def make_afe() -> dict:
    """Return a valid synthetic artifact; all numeric values are test-only."""
    artifact = {
        "artifact_type": "BMS_V1_AFE_STARTUP_PROTECTION_POLICY",
        "schema_version": 2,
        "schema_id": validator.AFE_SCHEMA_ID,
        "revision": "synthetic-afe-r1",
        "hardware_identity": copy.deepcopy(make_ntc()["hardware_identity"]),
        "device_basis": {
            "datasheet_revision": "SYNTHETIC_BQ_DS_R1",
            "errata_revision": "NONE",
            "policy_revision": "synthetic-afe-r1",
        },
        "startup_policy": {
            "wake_probe": {
                "approved": True,
                "settle_ms": 10,
                "max_attempts": 3,
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
                "datasheet_basis": SYNTHETIC,
            },
            "sys_ctrl1": {
                "desired_byte": 24,
                "readback_mask": 255,
                "expected_masked_value": 24,
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
            },
            "sys_ctrl2_early": {
                "desired_byte": 0,
                "readback_mask": 255,
                "expected_masked_value": 0,
                "chg_state": "OFF",
                "dsg_state": "OFF",
                "fet_safe_off_invariant": True,
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
            },
            "sys_ctrl2_final": {
                "desired_byte": 64,
                "readback_mask": 255,
                "expected_masked_value": 64,
                "cc_en_required": True,
                "chg_state": "OFF",
                "dsg_state": "OFF",
                "delay_dis_permitted": False,
                "cc_oneshot_permitted": False,
                "reserved_bits_safe": True,
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
            },
            "cellbal_startup": {
                "approved": True,
                "cellbal1_desired_byte": 0,
                "cellbal2_desired_byte": 0,
                "cellbal3_desired_byte": 0,
                "exact_readback_required": True,
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
            },
            "cc_cfg": {
                "desired_byte": 25,
                "readback_mask": 255,
                "expected_masked_value": 25,
                "datasheet_basis": SYNTHETIC,
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
            },
            "sys_stat": {
                "initial_blocking_mask": 31,
                "final_blocking_mask": 31,
                "read_owner": "BMS_AfeStartup",
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
                "xready_handling_identity": "CURRENT_FINAL_STATUS_READ_SINGLE_W1C_THEN_FULL_RECONFIGURE",
            },
            "initial_data_settle": {
                "duration_ms": 800,
                "transition_semantics": "ELAPSED_TIME_THEN_READ_FINAL_SYS_STAT",
                "failure_action": "FAIL_CLOSED_SAFE_OFF",
            },
            "cc_ready_ownership": {
                "w1c_owner": "ProtectTask",
                "startup_behavior": "PRESERVE",
                "runtime_consumer": "ProtectTask",
            },
        },
        "hardware_protection_policy": {
            "ov": {
                "target_cell_mv": 4200,
                "hardware_delay": {"value": 1, "unit": "s"},
                "delay_register": "PROTECT3",
                "delay_code": 0,
                "trip_encoding_driver": "BQ76940_Control_EncodeOvTrip",
                "runtime_calibration_required": True,
                "readback_required": True,
            },
            "uv": {
                "target_cell_mv": 3000,
                "hardware_delay": {"value": 1, "unit": "s"},
                "delay_register": "PROTECT3",
                "delay_code": 0,
                "trip_encoding_driver": "BQ76940_Control_EncodeUvTrip",
                "runtime_calibration_required": True,
                "readback_required": True,
            },
            "ocd": {
                "target": {"basis": "sense_voltage_mv", "sense_voltage_mv": 17},
                "threshold_code": 0,
                "selected_sense_voltage_mv": 17,
                "hardware_delay": {"value": 8, "unit": "ms"},
                "delay_register": "PROTECT2",
                "delay_code": 0,
                "selection_policy": "NOT_BELOW_REQUESTED",
                "readback_required": True,
            },
            "scd": {
                "target": {"basis": "sense_voltage_mv", "sense_voltage_mv": 44},
                "threshold_code": 0,
                "selected_sense_voltage_mv": 44,
                "hardware_delay": {"value": 70, "unit": "us"},
                "delay_register": "PROTECT1",
                "delay_code": 0,
                "selection_policy": "NOT_BELOW_REQUESTED",
                "readback_required": True,
            },
        },
        "current_rsense_policy": {
            "rsense_nominal_uohm": 1000,
            "rsense_source": SYNTHETIC,
            "rsense_approval_basis": SYNTHETIC,
            "current_polarity": 1,
            "polarity_definition": SYNTHETIC,
            "rsns_bit": 1,
            "cc_conversion_basis": SYNTHETIC,
        },
        "runtime_ownership_invariants": {
            "runtime_xready_w1c_owner": "ProtectTask",
            "startup_xready_w1c_exception": "BMS_AfeStartup",
            "runtime_recovery_coordinator": "RecoveryCoordinator",
            "runtime_recovery_serviced_by": "StateTask",
            "runtime_calibration_handoff_owner": "RecoveryCoordinator",
            "default_calibration_permitted": False,
            "xready_generation_binding_required": True,
            "post_clear_provenance_required": True,
            "first_valid_measurement_proof": {
                "sample_sequence": True,
                "afe_generation": True,
                "event_only_proof_permitted": False,
            },
            "generic_latch_clear_permitted": False,
        },
        "runtime_calibration_contract": {
            "adc_gain1_read_every_startup_recovery": True,
            "adc_offset_read_every_startup_recovery": True,
            "adc_gain2_read_every_startup_recovery": True,
            "read_failure_action": "FAIL_CLOSED_NO_HANDOFF",
            "invalid_decode_action": "FAIL_CLOSED_NO_HANDOFF",
            "handoff_api": "BMS_Sample_SetCalibration",
            "static_fallback_permitted": False,
        },
        "fail_closed_fallback": {
            "recovery_in_progress": {"chg_inhibit": True, "dsg_inhibit": True},
            "xready_historical_latch": {
                "action_bearing": True,
                "chg_inhibit": True,
                "dsg_inhibit": True,
                "duration": "UNTIL_FUTURE_POLICY",
            },
            "scd_historical_latch": {
                "action_bearing": True,
                "chg_inhibit": True,
                "dsg_inhibit": True,
                "duration": "UNTIL_FUTURE_POLICY",
            },
            "ovrd_alert_historical_latch": {
                "action_bearing": True,
                "chg_inhibit": True,
                "dsg_inhibit": True,
                "duration": "UNTIL_FUTURE_POLICY",
            },
        },
        "production_mapping": {
            "target_type": "BMS_AfeStartupConfig_t",
            "initializer_bindings": _initializer_bindings(),
            "fixed_register_policy_bindings": _register_bindings(),
            "runtime_invocation": {
                "initializer_api": "BMS_AfeStartup_Init",
                "step_api": "BMS_AfeStartup_Step",
                "production_call_required": True,
            },
        },
        "approval": {
            "status": "APPROVED",
            "approved_by": "SYNTHETIC_TEST_APPROVER",
            "approved_at": "2026-01-02T03:04:05Z",
            "approval_record": "synthetic-afe-approval-r1",
            "artifact_sha256": ZERO_HASH,
        },
    }
    return seal(artifact)


def dump_bytes(value: dict) -> bytes:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def make_record(artifact: dict, repository_path: str, artifact_raw: bytes | None = None) -> dict:
    return {
        "artifact_type": "BMS_V1_ARTIFACT_APPROVAL_RECORD",
        "schema_version": 1,
        "record_id": artifact["approval"]["approval_record"],
        "decision": "APPROVED",
        "artifact": {
            "artifact_type": artifact["artifact_type"],
            "artifact_schema_version": artifact["schema_version"],
            "artifact_schema_id": artifact["schema_id"],
            "artifact_revision": artifact["revision"],
            "artifact_canonicalization_id": validator.CANONICALIZATION_ID,
            "artifact_canonical_projection_sha256": artifact["approval"]["artifact_sha256"],
            "artifact_repository_path": repository_path,
            "artifact_git_blob_sha": ZERO_OID if artifact_raw is None else validator.git_blob_oid(artifact_raw),
        },
        "hardware_identity": copy.deepcopy(artifact["hardware_identity"]),
        "approver": {
            "identity": artifact["approval"]["approved_by"],
            "approved_at": artifact["approval"]["approved_at"],
        },
        "review": {
            "evidence_reference": SYNTHETIC,
            "decision_notes_or_reference": SYNTHETIC,
        },
    }


def schema_binding(kind: str) -> dict:
    path = validator.SCHEMA_PATHS[kind]
    raw = path.read_bytes()
    return {
        "schema_id": validator.NTC_SCHEMA_ID if kind == "ntc" else validator.AFE_SCHEMA_ID,
        "schema_version": 2,
        "schema_repository_path": path.relative_to(validator.REPO_ROOT).as_posix(),
        "schema_sha256": hashlib.sha256(raw).hexdigest(),
        "schema_git_commit": TEST_COMMIT,
        "schema_git_blob_sha": validator.git_blob_oid(raw),
    }


def make_manifest(ntc: dict, ntc_raw: bytes, ntc_record: dict, ntc_record_raw: bytes,
                  afe: dict, afe_raw: bytes, afe_record: dict, afe_record_raw: bytes) -> dict:
    manifest = {
        "artifact_type": "BMS_V1_PHASE8_GATE_MANIFEST",
        "schema_version": 1,
        "manifest_revision": "synthetic-manifest-r1",
        "hardware_identity": copy.deepcopy(ntc["hardware_identity"]),
        "candidate": {
            "repository": SYNTHETIC,
            "branch_or_reference": "synthetic-test-branch",
            "production_git_commit": TEST_COMMIT,
        },
        "schemas": {"ntc": schema_binding("ntc"), "afe": schema_binding("afe")},
        "artifacts": {},
        "approval_records": {},
        "generated_outputs": {},
        "evidence": {
            "production_git_commit": TEST_COMMIT,
            "build_revision": SYNTHETIC,
            "test_revision": SYNTHETIC,
            "verifier_revision": SYNTHETIC,
            "evidence_manifest_or_reference": SYNTHETIC,
        },
    }
    values = {
        "ntc": (ntc, ntc_raw, ntc_record, ntc_record_raw, "ntc.json", "ntc-approval.json"),
        "afe": (afe, afe_raw, afe_record, afe_record_raw, "afe.json", "afe-approval.json"),
    }
    for name, (artifact, artifact_raw, record, record_raw, artifact_path, record_path) in values.items():
        manifest["artifacts"][name] = {
            "path": artifact_path,
            "revision": artifact["revision"],
            "canonical_projection_sha256": artifact["approval"]["artifact_sha256"],
            "git_blob_sha": validator.git_blob_oid(artifact_raw),
        }
        manifest["approval_records"][name] = {
            "path": record_path,
            "record_id": record["record_id"],
            "raw_file_sha256": hashlib.sha256(record_raw).hexdigest(),
            "git_blob_sha": validator.git_blob_oid(record_raw),
        }
        manifest["generated_outputs"][name] = {
            "path": f"synthetic/{name}_generated.c",
            "raw_file_sha256": "c" * 64,
            "production_git_commit": TEST_COMMIT,
            "source_artifact_revision": artifact["revision"],
            "source_artifact_canonical_projection_sha256": artifact["approval"]["artifact_sha256"],
        }
    return manifest


class CanonicalizationTests(unittest.TestCase):
    def test_golden_vectors(self) -> None:
        vector_path = Path(__file__).with_name("testdata") / "canonical_vectors.json"
        data = validator.load_strict_json(vector_path)
        self.assertEqual(data["algorithm_id"], validator.CANONICALIZATION_ID)
        hashes: dict[str, str] = {}
        for vector in data["vectors"]:
            with self.subTest(vector=vector["name"]):
                if "raw_hex" in vector:
                    raw = bytes.fromhex(vector["raw_hex"])
                else:
                    raw = vector["raw_text"].encode("utf-8")
                raw = bytes.fromhex(vector.get("prepend_hex", "")) + raw
                if vector["expected_valid"]:
                    value = validator.load_strict_json_bytes(raw)
                    digest = validator.canonical_projection_sha256(value)
                    self.assertEqual(digest, vector["expected_hash"])
                    hashes[vector["name"]] = digest
                else:
                    with self.assertRaises((validator.StrictJsonError, validator.ValidationFailure)):
                        value = validator.load_strict_json_bytes(raw)
                        validator.canonical_projection_sha256(value)
        for vector in data["vectors"]:
            if "expected_same_hash_as" in vector:
                self.assertEqual(hashes[vector["name"]], hashes[vector["expected_same_hash_as"]])
            if "expected_different_hash_from" in vector:
                self.assertNotEqual(hashes[vector["name"]], hashes[vector["expected_different_hash_from"]])

    def test_hash_field_mutation_does_not_change_projection(self) -> None:
        artifact = make_ntc()
        original = validator.canonical_projection_sha256(artifact)
        artifact["approval"]["artifact_sha256"] = "f" * 64
        self.assertEqual(original, validator.canonical_projection_sha256(artifact))

    def test_other_field_mutation_changes_projection(self) -> None:
        artifact = make_ntc()
        original = validator.canonical_projection_sha256(artifact)
        artifact["revision"] = "synthetic-ntc-r2"
        self.assertNotEqual(original, validator.canonical_projection_sha256(artifact))


class SchemaAndArtifactTests(unittest.TestCase):
    def assert_ntc_invalid(self, mutate) -> None:
        artifact = make_ntc()
        mutate(artifact)
        seal(artifact)
        with self.assertRaises(validator.ValidationFailure):
            validator.validate_artifact_object(artifact, "ntc")

    def assert_afe_invalid(self, mutate) -> None:
        artifact = make_afe()
        mutate(artifact)
        seal(artifact)
        with self.assertRaises(validator.ValidationFailure):
            validator.validate_artifact_object(artifact, "afe")

    def test_synthetic_fixtures_validate(self) -> None:
        validator.validate_artifact_object(make_ntc(), "ntc")
        validator.validate_artifact_object(make_afe(), "afe")

    def test_schema_self_checks(self) -> None:
        for kind in ("ntc", "afe", "approval", "manifest"):
            with self.subTest(kind=kind):
                schema = validator.load_strict_json(validator.SCHEMA_PATHS[kind])
                from jsonschema import Draft202012Validator
                Draft202012Validator.check_schema(schema)

    def test_uppercase_hash_rejected(self) -> None:
        artifact = make_ntc()
        artifact["approval"]["artifact_sha256"] = artifact["approval"]["artifact_sha256"].upper()
        with self.assertRaises(validator.ValidationFailure):
            validator.validate_artifact_object(artifact, "ntc")

    def test_bad_rfc3339_rejected(self) -> None:
        self.assert_ntc_invalid(lambda item: item["approval"].__setitem__("approved_at", "2026-01-02 03:04:05"))

    def test_placeholder_rejected(self) -> None:
        self.assert_ntc_invalid(lambda item: item["device"].__setitem__("variant", "TBD"))

    def test_ntc_model_confirmed_false(self) -> None:
        self.assert_ntc_invalid(lambda item: item["ts_conversion_basis"].__setitem__("model_confirmed", False))

    def test_ntc_point_count_mismatch(self) -> None:
        self.assert_ntc_invalid(lambda item: item["table"].__setitem__("point_count", 2))

    def test_ntc_duplicate_points(self) -> None:
        self.assert_ntc_invalid(lambda item: item["table"]["points"].__setitem__(1, copy.deepcopy(item["table"]["points"][0])))

    def test_ntc_resistance_monotonic_failure(self) -> None:
        self.assert_ntc_invalid(lambda item: item["table"]["points"][1].__setitem__("resistance_ohm", 500))

    def test_ntc_temperature_monotonic_failure(self) -> None:
        self.assert_ntc_invalid(lambda item: item["table"]["points"][1].__setitem__("temperature_decic", 1500))

    def test_ntc_ordering_mismatch(self) -> None:
        self.assert_ntc_invalid(lambda item: item["table"].__setitem__("ordering", "resistance_descending"))

    def test_ntc_coverage_mismatch(self) -> None:
        self.assert_ntc_invalid(lambda item: item["coverage"].__setitem__("minimum_temperature_decic", -900))

    def test_ntc_float_tolerance_impossible(self) -> None:
        raw = dump_bytes(make_ntc()).replace(b'"bias_resistor_tolerance_ppm":10000', b'"bias_resistor_tolerance_ppm":10000.0')
        with self.assertRaises(validator.StrictJsonError):
            validator.load_strict_json_bytes(raw)

    def test_ntc_ppm_domain(self) -> None:
        self.assert_ntc_invalid(lambda item: item["ts_conversion_basis"].__setitem__("bias_resistor_tolerance_ppm", 1000001))

    def test_afe_startup_on_rejected(self) -> None:
        self.assert_afe_invalid(lambda item: item["startup_policy"]["sys_ctrl2_final"].__setitem__("chg_state", "ON"))

    def test_afe_fail_closed_allow_impossible(self) -> None:
        for name in ("xready_historical_latch", "scd_historical_latch", "ovrd_alert_historical_latch"):
            with self.subTest(name=name):
                self.assert_afe_invalid(lambda item, field=name: item["fail_closed_fallback"][field].__setitem__("chg_inhibit", False))

    def test_afe_wrong_w1c_owner(self) -> None:
        self.assert_afe_invalid(lambda item: item["runtime_ownership_invariants"].__setitem__("runtime_xready_w1c_owner", "StateTask"))

    def test_afe_wrong_recovery_coordinator(self) -> None:
        self.assert_afe_invalid(lambda item: item["runtime_ownership_invariants"].__setitem__("runtime_recovery_coordinator", "ProtectTask"))

    def test_afe_default_calibration_true(self) -> None:
        self.assert_afe_invalid(lambda item: item["runtime_ownership_invariants"].__setitem__("default_calibration_permitted", True))

    def test_afe_generic_latch_clear_forbidden(self) -> None:
        self.assert_afe_invalid(lambda item: item["runtime_ownership_invariants"].__setitem__("generic_latch_clear_permitted", True))

    def test_afe_event_only_recovery_proof_forbidden(self) -> None:
        self.assert_afe_invalid(lambda item: item["runtime_ownership_invariants"]["first_valid_measurement_proof"].__setitem__("event_only_proof_permitted", True))

    def test_afe_delay_units_and_codes(self) -> None:
        cases = [
            ("ov", "ms", 0, 1),
            ("uv", "ms", 0, 1),
            ("ocd", "s", 0, 8),
            ("scd", "ms", 0, 70),
        ]
        for name, unit, code, value in cases:
            with self.subTest(name=name):
                self.assert_afe_invalid(lambda item, n=name, u=unit: item["hardware_protection_policy"][n]["hardware_delay"].__setitem__("unit", u))
                self.assert_afe_invalid(lambda item, n=name, c=code, v=value: item["hardware_protection_policy"][n]["hardware_delay"].__setitem__("value", v + 1))
                self.assert_afe_invalid(lambda item, n=name: item["hardware_protection_policy"][n].__setitem__("delay_code", 1))

    def test_afe_invalid_threshold_code(self) -> None:
        self.assert_afe_invalid(lambda item: item["hardware_protection_policy"]["ocd"].__setitem__("threshold_code", 16))

    def test_afe_independent_rsns_field_impossible(self) -> None:
        self.assert_afe_invalid(lambda item: item["hardware_protection_policy"]["ocd"].__setitem__("rsns_bit", 0))

    def test_afe_contradictory_target_form_impossible(self) -> None:
        self.assert_afe_invalid(lambda item: item["hardware_protection_policy"]["ocd"]["target"].__setitem__("current_ma", 1000))

    def test_afe_not_below_selection_mismatch(self) -> None:
        def mutate(item):
            item["hardware_protection_policy"]["ocd"]["target"]["sense_voltage_mv"] = 18
        self.assert_afe_invalid(mutate)

    def test_afe_current_basis_uses_exact_integer_rational_math(self) -> None:
        artifact = make_afe()
        artifact["hardware_protection_policy"]["ocd"]["target"] = {
            "basis": "current_ma",
            "current_ma": 17001,
        }
        artifact["hardware_protection_policy"]["ocd"]["threshold_code"] = 1
        artifact["hardware_protection_policy"]["ocd"]["selected_sense_voltage_mv"] = 22
        seal(artifact)
        validator.validate_artifact_object(artifact, "afe")

    def test_pair_identity_and_datasheet_mismatch(self) -> None:
        ntc = make_ntc()
        afe = make_afe()
        afe["hardware_identity"]["board_revision"] = "SYNTHETIC_TEST_BOARD_R2"
        with self.assertRaises(validator.ValidationFailure):
            validator.validate_pair(ntc, afe)
        afe = make_afe()
        afe["device_basis"]["datasheet_revision"] = "SYNTHETIC_BQ_DS_R2"
        with self.assertRaises(validator.ValidationFailure):
            validator.validate_pair(ntc, afe)

    def test_final_templates_are_expected_rejects(self) -> None:
        cases = [
            ("BMS_V1_NTC_Config.v2.template.json", "ntc"),
            ("BMS_V1_AFE_Policy.v2.template.json", "afe"),
        ]
        for filename, kind in cases:
            with self.subTest(filename=filename):
                template = validator.load_strict_json(validator.SCHEMA_DIR / filename)
                with self.assertRaises(validator.ValidationFailure):
                    validator.validate_artifact_object(template, kind)
        approval = validator.load_strict_json(validator.SCHEMA_DIR / "BMS_V1_Artifact_Approval_Record.v1.template.json")
        with self.assertRaises(validator.ValidationFailure):
            validator.validate_approval_record_object(approval)
        manifest = validator.load_strict_json(validator.SCHEMA_DIR / "BMS_V1_Phase8_Gate_Manifest.v1.template.json")
        with self.assertRaises(validator.ValidationFailure):
            validator._check_placeholders(manifest)


class ApprovalAndManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.ntc = make_ntc()
        self.afe = make_afe()
        self.ntc_raw = dump_bytes(self.ntc)
        self.afe_raw = dump_bytes(self.afe)
        self.ntc_record = make_record(self.ntc, "ntc.json", self.ntc_raw)
        self.afe_record = make_record(self.afe, "afe.json", self.afe_raw)
        self.ntc_record_raw = dump_bytes(self.ntc_record)
        self.afe_record_raw = dump_bytes(self.afe_record)

    def test_approval_binding_valid(self) -> None:
        validator.validate_approval_binding(self.ntc, self.ntc_record)

    def test_approval_self_assertion_is_not_external_approval(self) -> None:
        output = io.StringIO()
        result_tuple = (self.ntc, self.ntc["approval"]["artifact_sha256"], self.ntc_raw)
        with mock.patch.object(validator, "validate_artifact_file", return_value=result_tuple):
            with contextlib.redirect_stdout(output):
                result = validator.main(["--ntc", "synthetic-ntc.json"])
        self.assertEqual(result, 0)
        self.assertIn("external approval not verified", output.getvalue())

    def test_detached_approval_mismatches(self) -> None:
        mutations = [
            lambda record: record["artifact"].__setitem__("artifact_canonical_projection_sha256", "f" * 64),
            lambda record: record["artifact"].__setitem__("artifact_revision", "synthetic-ntc-r2"),
            lambda record: record["hardware_identity"].__setitem__("board_revision", "SYNTHETIC_TEST_BOARD_R2"),
            lambda record: record.__setitem__("record_id", "synthetic-wrong-record"),
        ]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                record = copy.deepcopy(self.ntc_record)
                mutate(record)
                with self.assertRaises(validator.ValidationFailure):
                    validator.validate_approval_binding(self.ntc, record)

    def _manifest(self) -> dict:
        return make_manifest(
            self.ntc,
            self.ntc_raw,
            self.ntc_record,
            self.ntc_record_raw,
            self.afe,
            self.afe_raw,
            self.afe_record,
            self.afe_record_raw,
        )

    def _validate_manifest(self, manifest: dict) -> None:
        validator.validate_manifest_object(
            manifest,
            self.ntc,
            self.ntc_record,
            self.afe,
            self.afe_record,
        )

    def test_manifest_preflight_valid(self) -> None:
        self._validate_manifest(self._manifest())

    def test_manifest_stale_artifact(self) -> None:
        manifest = self._manifest()
        manifest["artifacts"]["ntc"]["canonical_projection_sha256"] = "f" * 64
        with self.assertRaises(validator.ValidationFailure):
            self._validate_manifest(manifest)

    def test_manifest_wrong_approval_record(self) -> None:
        manifest = self._manifest()
        manifest["approval_records"]["afe"]["record_id"] = "synthetic-wrong-record"
        with self.assertRaises(validator.ValidationFailure):
            self._validate_manifest(manifest)

    def test_manifest_wrong_candidate_binding(self) -> None:
        manifest = self._manifest()
        manifest["generated_outputs"]["ntc"]["production_git_commit"] = "b" * 40
        with self.assertRaises(validator.ValidationFailure):
            self._validate_manifest(manifest)


class SourceTableRegressionTests(unittest.TestCase):
    def test_preflight_tables_match_current_control_source(self) -> None:
        source_path = validator.REPO_ROOT / "firmware" / "Driver" / "bq76940_control.c"
        compact = re.sub(r"\s+", "", source_path.read_text(encoding="utf-8"))
        sequences = [
            validator.OCD_THRESHOLD_MV[1],
            validator.OCD_THRESHOLD_MV[0],
            validator.OCD_DELAY_MS,
            validator.SCD_THRESHOLD_MV[1],
            validator.SCD_THRESHOLD_MV[0],
            validator.SCD_DELAY_US,
            validator.OV_DELAY_S,
            validator.UV_DELAY_S,
        ]
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                source_form = ",".join(f"{value}U" for value in sequence)
                self.assertIn(source_form, compact)


if __name__ == "__main__":
    unittest.main(verbosity=2)
