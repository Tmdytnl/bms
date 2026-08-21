#!/usr/bin/env python3
"""Verify the reproducible BMS V1 Phase 8 hard-gate evidence.

Exit status 0 means the evidence and production-integration gate pass.  Exit
status 1 means evidence is missing or a checked contract failed.  Exit status
2 means every executable/static check passed, but an explicitly named
production-policy input is still absent, so Phase 8 remains blocked.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


REPO = Path(__file__).resolve().parents[2]
FW = REPO / "firmware"
APP = FW / "App"
TESTS = FW / "Tests"
BUILD = TESTS / "Build" / "Phase8"
PROJECT_DIR = FW / "Project" / "Keil"
VERIFY_LOG = BUILD / "verify_phase8.log"
GATE_POLICY_REVISION = "phase8-hard-gate-redteam-r3-20260821"
UVOPTX_RELATIVE = "firmware/Project/Keil/BMS_V1.uvoptx"
SIMULATOR_INIT_TARGET = r"..\..\Tests\phase8_simulator.ini"
SAMPLE_STACK_WORDS = 192
SAMPLE_STACK_BYTES_PER_WORD = 4
SAMPLE_STACK_STATIC_MAX_BYTES = 512
SAMPLE_STACK_CONTEXT_RESERVE_BYTES = 64
SAMPLE_STACK_RUNTIME_RESERVE_BYTES = 192

IMAGE_NAMES = (
    "phase4_regression_tests",
    "phase6_regression_tests",
    "phase7_regression_tests",
    "phase8_data_tests",
    "phase8_sample_tests",
    "phase8_afe_tests",
)

results: list[str] = []
failures: list[str] = []
blockers: list[str] = []


def check(condition: bool, message: str) -> bool:
    if condition:
        results.append(f"PASS: {message}")
        return True
    failures.append(message)
    results.append(f"FAIL: {message}")
    return False


def block(message: str) -> None:
    if message not in blockers:
        blockers.append(message)
        results.append(f"BLOCKER: {message}")


def info(message: str) -> None:
    results.append(f"INFO: {message}")


def load(path: Path, label: str | None = None) -> str:
    name = label or str(path.relative_to(REPO))
    if not check(path.is_file(), f"artifact exists: {name}"):
        return ""
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        check(False, f"artifact is readable: {name} ({exc})")
        return ""


def function_body(source: str, signature: str) -> str:
    """Return one C function body with balanced braces, or an empty string."""
    start = source.find(signature)
    if start < 0:
        return ""
    brace = source.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    return ""


def linked_thumb_symbol(map_text: str, symbol: str) -> bool:
    """Require a linked Thumb definition, not an unused-section mention."""
    return re.search(
        rf"^\s*{re.escape(symbol)}\s+0x[0-9a-f]+\s+Thumb Code\s+\d+\b",
        map_text,
        re.IGNORECASE | re.MULTILINE,
    ) is not None


def linked_thumb_symbols(map_text: str) -> set[str]:
    return set(re.findall(
        r"^\s*([A-Za-z_]\w*)\s+0x[0-9a-f]+\s+Thumb Code\s+\d+\b",
        map_text,
        re.IGNORECASE | re.MULTILINE,
    ))


def linked_thumb_size(map_text: str, symbol: str) -> int | None:
    match = re.search(
        rf"^\s*{re.escape(symbol)}\s+0x[0-9a-f]+\s+Thumb Code\s+(\d+)\b",
        map_text,
        re.IGNORECASE | re.MULTILINE,
    )
    return int(match.group(1)) if match is not None else None


def parse_started_ns(value: str) -> int:
    normalized = value.strip()
    if normalized.endswith("Z"):
        normalized = normalized[:-1] + "+00:00"
    parsed = datetime.fromisoformat(normalized)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return int(parsed.timestamp() * 1_000_000_000)


def verify_project() -> tuple[list[Path], str]:
    project_path = PROJECT_DIR / "BMS_V1.uvprojx"
    project_text = load(project_path)
    production_sources: list[Path] = []
    try:
        root = ET.fromstring(project_text)
    except ET.ParseError as exc:
        check(False, f"production Keil project parses as XML ({exc})")
        return production_sources, project_text

    values = [node.text for node in root.findall(".//FilePath") if node.text]
    check(bool(values), "production Keil target has source FilePath entries")
    check(all(not re.match(r"^(?:[A-Za-z]:[\\/]|[\\/]{2}|/)", value)
              for value in values),
          "all production Keil source paths are repository-relative")
    unresolved: list[str] = []
    outside: list[str] = []
    for value in values:
        candidate = (PROJECT_DIR / value.replace("\\", os.sep)).resolve()
        try:
            candidate.relative_to(REPO)
        except ValueError:
            outside.append(value)
        if not candidate.is_file():
            unresolved.append(value)
        production_sources.append(candidate)
    check(not outside, "all production Keil sources stay inside the repository")
    check(not unresolved, "all production Keil FilePath entries resolve")

    key_sources = {
        "bms_data.c", "bms_ntc.c", "bms_sample.c", "bms_afe_startup.c",
    }
    project_names = {path.name.lower() for path in production_sources}
    check({name.lower() for name in key_sources}.issubset(project_names),
          "production target declares data, NTC, Sample and AFE startup")

    options = load(PROJECT_DIR / "BMS_V1.uvoptx")
    matches = re.findall(r"<sIfile>([^<]*)</sIfile>", options)
    check(len(matches) == 1, "Keil options contain exactly one Simulator init")
    if len(matches) == 1:
        init_value = matches[0]
        is_absolute = re.match(
            r"^(?:[A-Za-z]:[\\/]|[\\/]{2}|/)", init_value
        ) is not None
        check(not is_absolute, "active Keil Simulator init is repository-relative")
        init_path = (PROJECT_DIR / init_value.replace("\\", os.sep)).resolve()
        check(init_path.is_file(), "active Keil Simulator init path resolves")
    check(str(REPO).lower() not in options.lower(),
          "Keil options contain no stale absolute workspace path")
    return production_sources, project_text


def verify_data_and_ntc_contracts() -> None:
    data_h = load(APP / "bms_data.h")
    data = load(APP / "bms_data.c")
    ntc_h = load(APP / "bms_ntc.h")
    ntc = load(APP / "bms_ntc.c")
    config = load(FW / "Config" / "bms_config.h")

    normalized_h = re.sub(r"\s+", " ", data_h)
    for declaration in (
        "bool BMS_Data_PublishMeasurement(const BMS_MeasurementFrame_t *frame);",
        "bool BMS_Data_GetSnapshot(BMS_DataSnapshot_t *snapshot, BMS_TimestampMs_t now_ms);",
        "bool BMS_Data_GetFreshnessSnapshot( BMS_DataFreshnessSnapshot_t *snapshot, BMS_TimestampMs_t now_ms);",
        "bool BMS_Data_IsFresh(bool valid, bool stale_latched, uint32_t age_ms, uint32_t max_age_ms);",
    ):
        check(declaration in normalized_h,
              f"controlled data API is declared: {declaration.split('(')[0]}")
    check(all(token in data_h for token in (
        "update_current", "update_temperature", "current_timestamp_ms",
        "temperature_timestamp_ms", "ts1_raw14", "ts1_resistance_ohm",
        "temperature_decic", "sample_sequence", "stale_latched",
        "stale_bitmap", "BMS_DataFreshnessSnapshot_t",
        "BMS_DATA_FRESHNESS_SNAPSHOT_MAX_BYTES",
    )), "measurement frame/snapshot expose independent CC and TS groups")
    check("#define BMS_CELL_COUNT" in config and
          "#define BMS_CELL_DEFINED_MASK" in data_h,
          "data contract defines the 13-cell frame and valid mask")
    check(all(token in config for token in (
        "BMS_VOLTAGE_FRESH_LIMIT_MS", "BMS_CURRENT_FRESH_LIMIT_MS",
        "BMS_TEMPERATURE_FRESH_LIMIT_MS",
    )) and all(token in data_h for token in (
        "BMS_DATA_VOLTAGE_FRESH_MAX_MS",
        "BMS_DATA_CURRENT_FRESH_MAX_MS",
        "BMS_DATA_TEMPERATURE_FRESH_MAX_MS",
    )), "data freshness limits alias the authoritative Config constants")

    publish = function_body(data, "bool BMS_Data_PublishMeasurement")
    get_snapshot = function_body(data, "bool BMS_Data_GetSnapshot")
    get_freshness = function_body(
        data, "bool BMS_Data_GetFreshnessSnapshot"
    )
    init = function_body(data, "void BMS_Data_Init(void)")
    is_fresh = function_body(data, "bool BMS_Data_IsFresh")
    check(bool(publish) and bool(get_snapshot) and bool(get_freshness),
          "publish, full snapshot and bounded freshness implementations are present")
    check("xSemaphoreTake(xDataMutex, (TickType_t)0U)" in publish and
          "xSemaphoreTake(xDataMutex, (TickType_t)0U)" in get_snapshot and
          "xSemaphoreTake(xDataMutex, (TickType_t)0U)" in get_freshness,
          "data publish/read APIs use fail-safe zero-wait xDataMutex acquisition")
    check(publish.find("BMS_Data_FrameIsValid(frame)") <
          publish.find("xSemaphoreTake(xDataMutex") and
          "frame->cell_valid_bitmap != BMS_CELL_DEFINED_MASK" in data and
          "!frame->bq_pack_valid" in data,
          "core publication rejects null/partial-cell/BQ-pack-invalid frames")
    check("UINT32_MAX - pack_sum_mv" in publish,
          "cell-sum pack publication checks arithmetic overflow")
    check(publish.find("++g_bms_data.sample_sequence") >
          publish.find("xSemaphoreTake(xDataMutex") and
          "UINT32_MAX" not in publish[publish.find("sample_sequence") - 80:
                                      publish.find("sample_sequence") + 160],
          "successful core publication advances sequence once with natural wrap")
    check("g_bms_data.state" not in publish and
          "g_bms_data.faults" not in publish and
          "g_bms_data.soc_" not in publish and
          "g_bms_data.remaining_capacity" not in publish,
          "measurement-owned publication preserves state/fault/SOC/capacity")
    take = get_snapshot.find("xSemaphoreTake(xDataMutex")
    latch = get_snapshot.find("BMS_Data_LatchStale(now_ms)")
    output_copy = get_snapshot.find("*snapshot = g_bms_data")
    give = get_snapshot.find("xSemaphoreGive(xDataMutex)")
    age = get_snapshot.find("BMS_Data_DeriveAge")
    check(0 <= take < latch < output_copy < give < age and
          "BMS_DataSnapshot_t local" not in get_snapshot,
          "full snapshot copies directly under mutex then derives ages off-lock")
    fresh_take = get_freshness.find("xSemaphoreTake(xDataMutex")
    fresh_latch = get_freshness.find("BMS_Data_LatchStale(now_ms)")
    fresh_pack = get_freshness.find(
        "snapshot->pack_metadata = g_bms_data.pack_metadata"
    )
    fresh_sequence = get_freshness.find(
        "snapshot->sample_sequence = g_bms_data.sample_sequence"
    )
    fresh_give = get_freshness.find("xSemaphoreGive(xDataMutex)")
    fresh_age = get_freshness.find("BMS_Data_DeriveMetadataAge")
    check(0 <= fresh_take < fresh_latch < fresh_pack < fresh_sequence <
          fresh_give < fresh_age,
          "bounded freshness projection is one generation and aged off-lock")
    init_helper = function_body(data, "static void BMS_Data_InitMeasurement")
    check("metadata->valid = false" in init_helper and
          "metadata->stale_latched = false" in init_helper and
          "cell_metadata.stale_bitmap = (uint16_t)0U" in init and
          "BMS_DATA_AGE_UNKNOWN_MS" in init_helper and
          init.count("BMS_Data_InitMeasurement") >= 6 and
          "sample_sequence = (uint32_t)0U" in init,
          "startup snapshot is invalid with unknown age and zero sequence")
    check("now_ms - timestamp_ms" in data,
          "measurement age uses wrap-safe unsigned subtraction")
    check(all(token in data_h + data for token in (
        "BMS_Data_LatchStale", "stale_latched", "stale_bitmap",
    )) and all(token in is_fresh for token in (
        "valid", "stale_latched", "age_ms", "max_age_ms",
        "BMS_DATA_AGE_UNKNOWN_MS",
    )), "sticky stale, validity and wrap-safe age remain separate decisions")
    data_test = load(TESTS / "test_phase8_data.c")
    sample_test = load(TESTS / "test_phase8_sample.c")
    check(all(token in data_test + sample_test for token in (
        "TestData_StickyStaleCannotResurrectAfterWrap",
        "TestData_FreshnessProjectionIsOneGeneration",
        "TestSample_BoundedStaleProjection",
    )), "production-C suites cover sticky wrap and bounded freshness projection")

    check(all(token in ntc_h + ntc for token in (
        "BMS_Ntc_ValidateTable", "BMS_Ntc_Interpolate",
        "point_count < 2U", "resistance_ascending",
    )), "NTC module validates and interpolates configurable monotonic tables")
    check("int64_t resistance_delta" in ntc and
          "int64_t interpolated" in ntc,
          "NTC interpolation uses overflow-safe wide intermediates")
    production_ntc_declarations = re.findall(
        r"(?:static\s+)?(?:const\s+)?BMS_NtcPoint_t\s+\w+\s*\[",
        data_h + data + ntc_h + ntc,
    )
    check(not production_ntc_declarations,
          "production data/NTC modules contain no built-in NTC curve")


def verify_sample_and_mailbox_contracts() -> None:
    sample_h = load(APP / "bms_sample.h")
    sample = load(APP / "bms_sample.c")
    rtos = load(APP / "app_rtos.c")
    protect_h = load(APP / "bms_protect.h")
    protect = load(APP / "bms_protect.c")
    sample_test = load(TESTS / "test_phase8_sample.c")
    sample_stub_h = load(TESTS / "test_phase8_sample_stub.h")
    sample_stub = load(TESTS / "test_phase8_sample_stub.c")
    phase7_test = load(TESTS / "test_phase7_logic.c")

    forbidden = ("BQ76940_ReadCcRaw", "xQueueReceive", "xQueuePeek",
                 "xCcSampleQueue", "g_bms_data")
    check(all(token not in sample for token in forbidden),
          "Sample performs no direct CC read/queue consume/global-data access")
    check("BMS_Protect_GetLatestCc" in sample and
          "BMS_Protect_GetLatestCc" in protect and
          "BMS_PROTECT_CC_SEQUENCE_NEXT" in protect_h,
          "Sample consumes the scheduler-coherent latest-current mailbox")
    check("+ 1UL" in protect_h and "UINT32_MAX" not in
          function_body(protect, "bool BMS_Protect_GetLatestCc"),
          "latest-current sequence has intentional natural unsigned wrap")

    latest_cc_match = re.search(
        r"typedef\s+struct\s*\{(?P<body>[^}]*)\}\s*"
        r"BMS_ProtectLatestCc_t\s*;",
        protect_h,
        re.DOTALL,
    )
    latest_cc_fields = (latest_cc_match.group("body")
                        if latest_cc_match is not None else "")
    push_cc = function_body(protect, "bool BMS_Protect_PushCcSample")
    latest_get = function_body(protect, "bool BMS_Protect_GetLatestCc")
    check(all(token in latest_cc_fields for token in (
        "uint32_t sequence", "uint32_t xready_generation", "bool valid",
    )), "latest-current mailbox carries an explicit XREADY epoch tag")
    push_inactive = push_cc.find(
        "if ((inserted == pdPASS) && !s_xready_state.active)"
    )
    push_generation = push_cc.find(
        "s_latest_cc.xready_generation =", push_inactive
    )
    push_valid = push_cc.find("s_latest_cc.valid = true", push_inactive)
    push_active = push_cc.find(
        "else if ((inserted == pdPASS) && s_xready_state.active)",
        push_inactive,
    )
    push_invalid = push_cc.find("s_latest_cc.valid = false", push_active)
    push_resume = push_cc.find("xTaskResumeAll()", push_invalid)
    check(0 <= push_inactive < push_generation < push_valid < push_active <
          push_invalid < push_resume,
          "Protect tags inactive-epoch CC and withholds active-epoch CC from latest")
    check("*snapshot = s_latest_cc" in latest_get and
          "snapshot->valid && !s_xready_state.active" in latest_get and
          "snapshot->xready_generation ==" in latest_get and
          "s_xready_state.xready_generation" in latest_get and
          "snapshot->valid = false" in latest_get and
          "vTaskSuspendAll()" in latest_get and
          "xTaskResumeAll()" in latest_get,
          "latest-current getter rejects active or epoch-mismatched mailboxes")

    xready_get = function_body(protect, "bool BMS_Protect_GetXreadyState")
    xready_binding = function_body(
        protect, "bool BMS_Protect_XreadyBindingIsCurrent"
    )
    drain = function_body(protect, "BMS_ProtectDrainResult_t BMS_Protect_Drain")
    recover = function_body(protect, "bool BMS_Protect_RecoverXready")
    resolve = function_body(
        protect, "static void BMS_Protect_ResolveObservedLowW1c"
    )
    check(all(token in protect_h for token in (
        "BMS_ProtectXreadyState_t", "xready_generation", "active",
        "BMS_PROTECT_XREADY_GENERATION_NEXT",
        "BMS_Protect_GetXreadyState",
        "BMS_Protect_XreadyBindingIsCurrent",
    )), "Protect exposes the narrow XREADY generation/active contract")
    check("vTaskSuspendAll()" in xready_get and
          "*snapshot = s_xready_state" in xready_get and
          "xTaskResumeAll()" in xready_get,
          "Protect publishes one scheduler-coherent XREADY state snapshot")
    transition = drain.find(
        "if ((stat & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)"
    )
    transition_suspend = drain.rfind("vTaskSuspendAll()", 0, transition)
    transition_decide = drain.rfind("BMS_Protect_Decide", 0, transition)
    transition_increment = drain.find(
        "BMS_PROTECT_XREADY_GENERATION_NEXT", transition
    )
    transition_active = drain.find(
        "s_xready_state.active = true", transition
    )
    transition_resume = drain.find("xTaskResumeAll()", transition)
    transition_latest_invalid = drain.find(
        "s_latest_cc.valid = false", transition
    )
    transition_cc_handle = drain.find(
        "BMS_Protect_HandleCcReady", transition
    )
    check(0 <= transition_suspend < transition_decide < transition <
          transition_increment < transition_active < transition_resume and
          "if (!s_xready_state.active)" in drain[transition:
                                                     transition_increment],
          "first inactive-to-active XREADY observation advances generation atomically")
    check(transition_increment < transition_latest_invalid <
          transition_active < transition_resume < transition_cc_handle,
          "same-status XREADY invalidates latest before CC_READY is serviced")
    check("s_xready_state.active = false" in recover and
          "s_xready_state.active = false" in resolve and
          protect.count("s_xready_state.xready_generation = 0UL") == 1,
          "recovery clears active without rewinding the XREADY generation")
    check("!state->active" in xready_binding and
          "state->xready_generation == bound_generation" in xready_binding and
          "+ 1UL" in protect_h,
          "XREADY binding requires inactive exact generation with natural wrap")

    run_once = function_body(sample, "bool BMS_Sample_RunOnce")
    set_device = function_body(sample, "void BMS_Sample_SetDevice")
    set_calibration = function_body(
        sample, "bool BMS_Sample_SetCalibration"
    )
    set_ntc = function_body(sample, "bool BMS_Sample_SetNtcTable")
    task = function_body(sample, "void Task_Sample")
    check(bool(run_once) and bool(task) and
          "vTaskDelayUntil" in task and "BMS_Sample_RunOnce" in task,
          "Task_Sample is a periodic production task, not a placeholder")
    check("void Task_Sample" not in rtos,
          "app_rtos does not retain a competing Sample placeholder body")
    check("xDataMutex" not in sample and "xI2CMutex" not in
          load(APP / "bms_data.c"),
          "I2C and data mutex ownership stays in disjoint modules")
    publish_pos = run_once.find("BMS_Data_PublishMeasurement")
    check(run_once.count("xSemaphoreTake(xI2CMutex") == 3 and
          run_once.count("xSemaphoreGive(xI2CMutex") == 3 and
          0 <= run_once.rfind("xSemaphoreGive(xI2CMutex") < publish_pos,
          "Sample releases every group I2C lock before atomic publication")
    first_i2c_take = run_once.find("xSemaphoreTake(xI2CMutex")
    pre_xready_get = run_once.find("BMS_Protect_GetXreadyState")
    pre_binding = run_once.find("BMS_Protect_XreadyBindingIsCurrent")
    last_i2c_give = run_once.rfind("xSemaphoreGive(xI2CMutex")
    final_suspend = run_once.rfind("vTaskSuspendAll()", 0, publish_pos)
    final_xready_get = run_once.rfind(
        "BMS_Protect_GetXreadyState", 0, publish_pos
    )
    final_binding = run_once.rfind(
        "BMS_Protect_XreadyBindingIsCurrent", 0, publish_pos
    )
    final_resume = run_once.find("xTaskResumeAll()", publish_pos)
    check(0 <= pre_xready_get < pre_binding < first_i2c_take,
          "Sample rejects active/mismatched XREADY generation before AFE reads")
    check(0 <= last_i2c_give < final_suspend < final_xready_get <
          final_binding < publish_pos < final_resume and
          "if (xready_guard_current && configuration_current &&" in run_once and
          "ntc_config_current)" in run_once,
          "Sample rechecks generation and zero-wait publishes in one scheduler epoch")
    check(0 <= set_calibration.find("BMS_Sample_BeginConfigUpdate") <
          set_calibration.find("BMS_Protect_GetXreadyState") <
          set_calibration.find("s_calibration = *calibration") <
          set_calibration.find("BMS_Sample_EndConfigUpdate") and
          "!xready_state.active" in set_calibration and
          "s_calibration_generation_bound = false" in set_calibration,
          "calibration binds atomically to an inactive XREADY generation")
    set_device_get = set_device.find("BMS_Protect_GetLatestCc")
    set_device_assign = set_device.find("s_device = device")
    set_device_calibration_invalid = set_device.find(
        "s_calibration.valid = false"
    )
    set_device_consumes_sequence = set_device.find(
        "s_last_published_cc_sequence = latest_cc.sequence"
    )
    set_device_consumes_generation = set_device.find(
        "s_last_published_cc_xready_generation ="
    )
    set_device_pending = set_device.find(
        "s_current_epoch_invalidation_pending = true"
    )
    set_device_revision = set_device.find("s_configuration_revision =")
    check(0 <= set_device.find("BMS_Sample_BeginConfigUpdate") <
          set_device_get < set_device_assign < set_device_calibration_invalid <
          set_device_consumes_sequence < set_device_consumes_generation <
          set_device_pending < set_device_revision <
          set_device.find("BMS_Sample_EndConfigUpdate"),
          "SetDevice creates a new physical AFE boundary and cuts off old CC identity")
    check(all("s_configuration_revision =" in body for body in (
        set_device, set_calibration, set_ntc,
    )) and
          all("BMS_SAMPLE_CONFIGURATION_REVISION_NEXT" in body for body in (
              set_device, set_calibration, set_ntc,
          )) and
          "s_configuration_revision == configuration_revision" in run_once and
          "s_current_epoch_invalidation_pending ==" in run_once and
          "current_epoch_invalidation_pending" in run_once,
          "configuration revision and pending state reject same-value setter ABA")
    check("#define BMS_SAMPLE_CONFIGURATION_REVISION_NEXT" in sample_h and
          "+ 1UL" in sample_h and
          "captured_revision = UINT32_MAX" in sample_test and
          "BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(captured_revision)" in
          sample_test and
          "current_revision == 0UL" in sample_test,
          "configuration revision advances with intentional immediate natural wrap")
    seam = "BMS_Sample_TestSeedConfigurationRevision"
    header_seam_guard = re.search(
        r"#if\s+defined\(TEST_PHASE8_SAMPLE_IMAGE\).*?"
        r"BMS_Sample_TestSeedConfigurationRevision.*?#endif",
        sample_h,
        re.DOTALL,
    )
    source_seam_guard = re.search(
        r"#if\s+defined\(TEST_PHASE8_SAMPLE_IMAGE\).*?"
        r"BMS_Sample_TestSeedConfigurationRevision.*?#endif",
        sample,
        re.DOTALL,
    )
    check(header_seam_guard is not None and source_seam_guard is not None,
          "configuration wrap seed seam is confined to the Phase 8 Sample image")
    cc_get = run_once.find("BMS_Protect_GetLatestCc")
    cc_epoch_match = run_once.find(
        "latest_cc.xready_generation ==", cc_get
    )
    cc_sequence_compare = run_once.find(
        "latest_cc.sequence != s_last_published_cc_sequence", cc_epoch_match
    )
    cc_generation_compare = run_once.find(
        "latest_cc.xready_generation !=", cc_sequence_compare
    )
    invalidation_branch = run_once.find(
        "else if (current_invalidation_required)", cc_generation_compare
    )
    invalidation_update = run_once.find(
        "frame.update_current = true", invalidation_branch
    )
    invalidation_invalid = run_once.find(
        "frame.current_valid = false", invalidation_update
    )
    pending_clear = run_once.find(
        "s_current_epoch_invalidation_pending = false", publish_pos
    )
    publish_success = run_once.rfind("if (publish_succeeded)",
                                     publish_pos, pending_clear)
    check(0 <= cc_get < cc_epoch_match < cc_sequence_compare <
          cc_generation_compare < invalidation_branch < invalidation_update <
          invalidation_invalid < publish_pos < publish_success < pending_clear,
          "Sample accepts CC only from its calibration epoch and retires old current atomically")
    check("current_epoch_invalidation_pending ||" in run_once and
          "s_last_published_core_xready_generation !=" in run_once and
          "s_last_published_cc_xready_generation =" in run_once and
          "s_last_published_core_xready_generation =" in run_once and
          run_once.count("s_current_epoch_invalidation_pending = false") == 1,
          "new-epoch publication tracks core/current epochs and clears invalidation only on success")
    check("BMS_Data_GetFreshnessSnapshot" in sample and
          "BMS_Data_GetSnapshot" not in sample and
          "BMS_DataSnapshot_t" not in sample and
          "BMS_Data_IsFresh" in sample and
          "s_stale_observed" in sample,
          "Sample observes stale transitions through the bounded controlled-data API")
    check("frame.update_temperature = true" in sample and
          "frame.ts1_valid = true" in sample and
          "ntc_curve_unavailable = true" in sample,
          "missing NTC curve preserves TS raw/resistance while Celsius is invalid")
    publish_failure = run_once.find("if (!publish_succeeded)")
    published_sequence = run_once.find(
        "s_last_published_cc_sequence =", publish_success
    )
    temperature_commit = run_once.find(
        "s_temperature_due = false", publish_failure
    )
    check("publish_succeeded = BMS_Data_PublishMeasurement(&frame)" in
          run_once and 0 <= publish_pos < publish_success <
          published_sequence < publish_failure < temperature_commit,
          "failed publication retains pending current/temperature ownership")
    check(all(token in sample_h for token in (
        "BMS_Sample_SetCalibration", "BMS_Sample_SetNtcTable",
        "BMS_Sample_RunOnce", "Task_Sample",
    )), "Sample exposes explicit calibration/table binding and bounded RunOnce")
    check(all(token in sample_test for token in (
        "Test_Phase8_Sample", "g_phase8_sample_test_failures",
        "g_phase8_sample_test_completed",
        "TestSample_ProtectI2cContentionModel",
        "g_phase8_sample_contention_completed",
        "protect_i2c_take_attempt_count",
        "protect_i2c_timeout_count",
        "TestSample_XreadyGenerationGuard",
        "inject_xready_after_cell_give",
        "inject_xready_after_pack_give",
        "pend_xready_transition_on_final_guard",
        "g_phase8_sample_xready_guard_completed",
        "g_phase8_sample_xready_cell_rejects",
        "g_phase8_sample_xready_pack_rejects",
        "g_phase8_sample_xready_wrap_rejects",
        "g_phase8_sample_xready_atomic_publishes",
    )), "Phase 8 Sample production-C suite exports the gate contract")
    check(all(token in phase7_test for token in (
        "BMS_Protect_GetXreadyState",
        "BMS_Protect_XreadyBindingIsCurrent",
        "BMS_PROTECT_XREADY_GENERATION_NEXT(UINT32_MAX)",
    )), "Phase 7 production-C regression covers coherent XREADY state and wrap")
    phase7_xready = function_body(
        phase7_test, "uint32_t Test_Phase7_Xready"
    )
    combined_stat = phase7_xready.find(
        "BMS_PROTECT_STAT_DEVICE_XREADY |"
    )
    combined_cc = phase7_xready.find(
        "BMS_PROTECT_STAT_CC_READY", combined_stat
    )
    combined_drain = phase7_xready.find(
        "BMS_Protect_Drain", combined_cc
    )
    combined_latest_reject = phase7_xready.find(
        "!BMS_Protect_GetLatestCc(&latest_cc)", combined_drain
    )
    combined_old_queue = phase7_xready.find(
        "cc_sample.raw == (int16_t)111", combined_latest_reject
    )
    combined_new_queue = phase7_xready.find(
        "cc_sample.raw == (int16_t)222", combined_old_queue
    )
    combined_new_epoch_push = phase7_xready.find(
        "BMS_Protect_PushCcSample((int16_t)333)", combined_new_queue
    )
    check(0 <= combined_stat < combined_cc < combined_drain <
          combined_latest_reject < combined_old_queue < combined_new_queue <
          combined_new_epoch_push and
          "latest_cc.xready_generation == 1UL" in
          phase7_xready[combined_new_epoch_push:],
          "Phase 7 regression proves combined XREADY+CC preserves queue but quarantines latest")
    sample_guard = function_body(
        sample_test, "static void TestSample_XreadyGenerationGuard"
    )
    check(all(token in sample_guard for token in (
        "A same-generation SetDevice still creates a new physical AFE epoch",
        "snapshot.current_metadata.timestamp_ms == 350UL",
        "SetDevice also consumes the identity of an unconsumed old-device",
        "current_convert_call_count == 0UL",
        "fail_data_take_ordinal = 2UL",
        "snapshot.current_ma == (BMS_CurrentMa_t)422",
        "An unconsumed old-epoch mailbox cannot become current after recovery",
        "latest_cc.xready_generation = UINT32_MAX",
        "g_phase8_sample_xready_wrap_rejects",
        "g_phase8_sample_xready_guard_completed = 1UL",
    )) and sample_guard.count(
        "BMS_Sample_SetDevice(TestPhase8SampleStub_Device())"
    ) >= 3,
          "Phase 8 regression binds SetDevice, old-data invalidation, pending retry, epoch mismatch and wrap")
    configuration_guard = function_body(
        sample_test, "static void TestSample_ConfigurationRevisionGuard"
    )
    configuration_wrap_guard = function_body(
        sample_test,
        "static void TestSample_ConfigurationRevisionImmediateWrapGuard",
    )
    sample_suite = function_body(sample_test, "uint32_t Test_Phase8_Sample")
    check(all(token in sample_stub_h + sample_stub for token in (
        "inject_ntc_configuration_aba_after_pack_give",
        "inject_ntc_configuration_wrap_after_pack_give",
        "configuration_aba_injection_count",
        "configuration_aba_set_success_count",
        "configuration_wrap_injection_count",
        "configuration_wrap_set_success_count",
    )), "Sample stub exposes deterministic ABA and immediate-wrap injection counters")
    check(all(token in configuration_guard for token in (
        "inject_ntc_configuration_aba_after_pack_give = true",
        "configuration_aba_injection_count == 1UL",
        "configuration_aba_set_success_count == 2UL",
        "TestSample_SameCore(&snapshot, &previous)",
        "diagnostics.configuration_not_ready_count == 1UL",
        "diagnostics.data_publish_failure_count == 0UL",
        "previous.sample_sequence + 1UL",
    )), "Phase 8 Sample regression dynamically rejects A-to-B-to-A configuration changes")
    check(all(token in configuration_wrap_guard for token in (
        "BMS_Sample_TestSeedConfigurationRevision(captured_revision)",
        "inject_ntc_configuration_wrap_after_pack_give = true",
        "configuration_wrap_injection_count == 1UL",
        "configuration_wrap_set_success_count == 1UL",
        "TestSample_SameCore(&snapshot, &previous)",
        "diagnostics.configuration_not_ready_count == 1UL",
        "previous.sample_sequence + 1UL",
    )), "Phase 8 Sample regression drives actual RunOnce across revision MAX-to-zero")
    check("TestSample_ConfigurationRevisionGuard();" in sample_suite and
          "TestSample_ConfigurationRevisionImmediateWrapGuard();" in
          sample_suite,
          "Sample suite unconditionally executes both configuration-revision guards")


def verify_afe_contracts() -> None:
    header = load(APP / "bms_afe_startup.h")
    source = load(APP / "bms_afe_startup.c")
    test = load(TESTS / "test_phase8_afe_startup.c")

    check(all(token in header for token in (
        "protect1_present", "protect2_present", "protect3_present",
        "ov_uv_trip_present", "BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS",
        "BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS",
    )), "AFE startup requires explicit protection policy presence")
    check("!config->ov_uv_trip_present" in source and
          "!config->protect1_present" in source and
          "!config->protect2_present" in source and
          "!config->protect3_present" in source,
          "AFE startup rejects incomplete policy instead of defaulting fields")
    check("BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS" in source and
          "BMS_AFE_WAKE_SETTLE_MS" in source and
          "BMS_AFE_INITIAL_DATA_SETTLE_MS" in source,
          "AFE startup is bounded and models mandatory settle intervals")
    check(source.count("BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS") >= 2 and
          "BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS" in source,
          "AFE startup treats ambiguous writes/W1C finalization as terminal")
    check("BMS_AfeStartup_ReadFinalStatus" in source and
          "BMS_AfeStartup_HasBlockingStatus(startup->final_sys_stat)" in source and
          "startup->state = BMS_AFE_STARTUP_STATE_COMPLETE" in source,
          "AFE startup requires a blocking-status-free final read before completion")
    check(all(token in header for token in (
        "BMS_AFE_STARTUP_STATE_SAFE_OFF_WRITE",
        "BMS_AFE_STARTUP_STATE_SAFE_OFF_VERIFY",
        "BMS_AFE_STARTUP_FAILURE_SAFE_OFF_UNCONFIRMED",
    )) and all(token in source for token in (
        "BMS_AfeStartup_SafeOffWrite",
        "BMS_AfeStartup_SafeOffVerify",
        "BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF",
        "BMS_AFE_STARTUP_FAILURE_SAFE_OFF_UNCONFIRMED",
    )), "AFE startup bounds final FET readback recovery with verified safe-off")
    check(all(token in source for token in (
        "startup->xready_clear_attempted = true",
        "startup->fet_off_confirmed = false",
        "startup->safe_outputs_confirmed = false",
        "startup->calibration.valid = false",
        "BMS_AfeStartup_StageEarlyRegisters(startup)",
    )), "XREADY W1C invalidates prior evidence and forces full reconfiguration")
    check(all(token in test for token in (
        "Test_Phase8_AfeStartup", "g_phase8_afe_startup_test_failures",
        "g_phase8_afe_startup_test_completed",
        "TestAfe_FinalFetReadbackUsesBoundedSafeOff",
        "TestAfe_SafeOffFailureModesAreTerminal",
        "TestAfe_InitialXreadyRetiredIsNotBlindCleared",
        "TestAfe_XreadyClearForcesFullReconfiguration",
        "TestAfe_XreadyMustReadLowAfterClear",
        "TestAfe_ConfigWriteAmbiguityIsNotReplayed",
        "TestAfe_XreadyAmbiguityIsTerminal",
    )), "AFE startup production-C suite covers fail-closed ambiguity paths")


def verify_manifest(build_log: str, production_sources: list[Path]) -> list[Path]:
    entries = re.findall(
        r"^INPUT_SHA256 ([0-9a-f]{64}) ([^\r\n]+)$",
        build_log,
        re.MULTILINE,
    )
    check(bool(entries), "build log contains a SHA-256 source manifest")
    check(len(entries) == len({path for _, path in entries}),
          "source manifest has no duplicate paths")
    manifest_paths: list[Path] = []
    canonical_lines: list[str] = []
    for expected_hash, relative in entries:
        posix = PurePosixPath(relative)
        safe = (not posix.is_absolute() and ".." not in posix.parts and
                re.match(r"^[A-Za-z]:", relative) is None)
        check(safe, f"manifest path is repository-relative: {relative}")
        candidate = (REPO / Path(*posix.parts)).resolve()
        try:
            candidate.relative_to(REPO)
        except ValueError:
            safe = False
        if not safe:
            continue
        manifest_paths.append(candidate)
        if not check(candidate.is_file(), f"manifest input exists: {relative}"):
            continue
        actual_hash = hashlib.sha256(candidate.read_bytes()).hexdigest()
        check(actual_hash == expected_hash,
              f"manifest hash matches current input: {relative}")
        canonical_lines.append(f"{expected_hash} {relative}\n")

    mandatory = {
        "firmware/Tests/build_phase8.ps1",
        "firmware/Tests/verify_phase8.py",
        "firmware/Tests/phase8_tests.sct",
        "firmware/Tests/phase8_simulator.ini",
        "firmware/App/bms_data.c",
        "firmware/App/bms_ntc.c",
        "firmware/App/bms_sample.c",
        "firmware/App/bms_afe_startup.c",
        "firmware/Tests/test_phase8_data.c",
        "firmware/Tests/test_phase8_sample.c",
        "firmware/Tests/test_phase8_sample.h",
        "firmware/Tests/test_phase8_sample_stub.c",
        "firmware/Tests/test_phase8_sample_stub.h",
        "firmware/Tests/test_phase8_afe_startup.c",
        "firmware/Project/Keil/BMS_V1.uvprojx",
        UVOPTX_RELATIVE,
        "firmware/User/main.c",
    }
    declared = {path for _, path in entries}
    check(mandatory.issubset(declared),
          "source manifest covers all Phase 8 production/test/gate inputs")
    production_relative = {
        path.relative_to(REPO).as_posix()
        for path in production_sources
        if path.is_relative_to(REPO)
    }
    check(production_relative.issubset(declared),
          "source manifest covers every production Keil source")
    digest = hashlib.sha256(
        "".join(sorted(canonical_lines)).encode("utf-8")
    ).hexdigest()
    info(f"Phase 8 source-manifest SHA256={digest} entries={len(entries)}")
    return manifest_paths


def verify_uvoptx_patch_evidence(build_log: str) -> None:
    def one(pattern: str, label: str) -> str:
        matches = re.findall(pattern, build_log, re.MULTILINE)
        check(len(matches) == 1, f"build log records exactly one {label}")
        return matches[0] if len(matches) == 1 else ""

    original_hash = one(
        rf"^UVOPTX_ORIGINAL_SHA256 ([0-9a-f]{{64}}) "
        rf"{re.escape(UVOPTX_RELATIVE)}$",
        "original uvoptx byte hash",
    )
    target = one(
        r"^UVOPTX_PATCH_TARGET_SIFILE ([^\r\n]+)$",
        "effective uvoptx Simulator target",
    )
    patch_count = one(
        r"^UVOPTX_PATCH_MATCH_COUNT (\d+)$",
        "uvoptx patched-span count",
    )
    patched_hash = one(
        r"^UVOPTX_PATCHED_SHA256 ([0-9a-f]{64})$",
        "effective patched uvoptx byte hash",
    )
    restored_hash = one(
        r"^UVOPTX_RESTORED_SHA256 ([0-9a-f]{64})$",
        "restored uvoptx byte hash",
    )
    manifest_hash = one(
        rf"^INPUT_SHA256 ([0-9a-f]{{64}}) "
        rf"{re.escape(UVOPTX_RELATIVE)}$",
        "uvoptx source-manifest hash",
    )

    options_path = REPO / UVOPTX_RELATIVE
    if not check(options_path.is_file(), "restored Keil uvoptx exists"):
        return
    live_bytes = options_path.read_bytes()
    live_hash = hashlib.sha256(live_bytes).hexdigest()
    check(bool(original_hash) and live_hash == original_hash,
          "live uvoptx bytes equal the pre-run original hash")
    check(bool(manifest_hash) and manifest_hash == original_hash,
          "uvoptx original byte hash is bound into the input manifest")
    check(bool(restored_hash) and restored_hash == original_hash,
          "runner records exact post-run uvoptx byte restoration")
    check(target == SIMULATOR_INIT_TARGET,
          "effective uvoptx target is the repository-relative Phase 8 INI")
    check(patch_count == "1",
          "runner patched exactly one uvoptx sIfile element")

    spans = list(re.finditer(br"<sIfile>[^<]*</sIfile>", live_bytes))
    check(len(spans) == 1,
          "restored uvoptx contains exactly one original sIfile element")
    if len(spans) != 1 or not target:
        return
    try:
        replacement = ("<sIfile>" + target + "</sIfile>").encode("ascii")
    except UnicodeEncodeError:
        check(False, "effective uvoptx sIfile target is ASCII")
        return
    span = spans[0]
    expected_effective = (
        live_bytes[:span.start()] + replacement + live_bytes[span.end():]
    )
    expected_hash = hashlib.sha256(expected_effective).hexdigest()
    check(bool(patched_hash) and patched_hash == expected_hash,
          "effective uvoptx hash differs only by the unique sIfile span")
    check(expected_effective != live_bytes and patched_hash != original_hash,
          "Simulator used a distinct temporary uvoptx revision")


def verify_sample_stack_evidence(build_log: str) -> Path:
    relative = (
        "firmware/Tests/Build/Phase8/"
        "BMS_V1_Phase8_production.callgraph.txt"
    )
    source_lines = re.findall(
        r"^PRODUCTION_CALLGRAPH_SOURCE ([^\r\n]+)$",
        build_log,
        re.MULTILINE,
    )
    hash_lines = re.findall(
        rf"^PRODUCTION_CALLGRAPH_SHA256 ([0-9a-f]{{64}}) "
        rf"{re.escape(relative)}$",
        build_log,
        re.MULTILINE,
    )
    summary_depth = re.findall(
        r"^PRODUCTION_TASK_SAMPLE_MAX_DEPTH_BYTES (\d+)$",
        build_log,
        re.MULTILINE,
    )
    summary_unknown = re.findall(
        r"^PRODUCTION_TASK_SAMPLE_UNKNOWN ([01])$",
        build_log,
        re.MULTILINE,
    )
    check(source_lines == ["firmware/Project/Keil/Objects/BMS_V1.htm"],
          "build records the unique Keil production callgraph source")
    check(len(hash_lines) == 1,
          "build records exactly one production callgraph evidence hash")
    check(len(summary_depth) == 1 and len(summary_unknown) == 1,
          "build records one Task_Sample stack-depth/unknown summary")

    evidence_path = REPO / relative
    evidence = load(evidence_path)
    if evidence_path.is_file() and len(hash_lines) == 1:
        actual_hash = hashlib.sha256(evidence_path.read_bytes()).hexdigest()
        check(actual_hash == hash_lines[0],
              "production callgraph evidence hash matches build record")

    stack_header = load(APP / "app_rtos.h")
    stack_define = re.search(
        r"^\s*#define\s+APP_RTOS_STACK_SAMPLE\s+"
        r"\(?\s*(\d+)\s*\)?\s*$",
        stack_header,
        re.MULTILINE,
    )
    check(stack_define is not None,
          "Sample task stack-word configuration is statically parseable")
    configured_words = (
        int(stack_define.group(1)) if stack_define is not None else 0
    )
    check(configured_words == SAMPLE_STACK_WORDS,
          f"APP_RTOS_STACK_SAMPLE remains {SAMPLE_STACK_WORDS} words")

    task_blocks = re.findall(
        r'<P><STRONG><a name="[^\"]+"></a>Task_Sample</STRONG>\s*'
        r'\(Thumb,.*?bms_sample\.o\(i\.Task_Sample\)\)'
        r'(.*?)(?=<P><STRONG>|</BODY>)',
        evidence,
        re.IGNORECASE | re.DOTALL,
    )
    check(len(task_blocks) == 1,
          "callgraph contains exactly one production Task_Sample block")
    task_block = task_blocks[0] if len(task_blocks) == 1 else ""
    depth_matches = re.findall(
        r"Max Depth\s*=\s*(\d+)(?:\s*\+\s*Unknown)?",
        task_block,
        re.IGNORECASE,
    )
    check(len(depth_matches) == 1,
          "Task_Sample callgraph reports exactly one static Max Depth")
    check(re.search(r"\+\s*Unknown", task_block, re.IGNORECASE) is None,
          "Task_Sample static call chain contains no Unknown stack depth")
    depth_bytes = int(depth_matches[0]) if len(depth_matches) == 1 else 0
    check(summary_depth == [str(depth_bytes)] and
          summary_unknown == ["0" if re.search(
              r"\+\s*Unknown", task_block, re.IGNORECASE
          ) is None else "1"],
          "build stack summary agrees with hashed callgraph evidence")
    check(0 < depth_bytes <= SAMPLE_STACK_STATIC_MAX_BYTES,
          f"Task_Sample ARMCC5 static Max Depth is <= "
          f"{SAMPLE_STACK_STATIC_MAX_BYTES} bytes")
    check("Call Chain = Task_Sample" in task_block and
          "BMS_Sample_RunOnce" in task_block,
          "Task_Sample callgraph evidence reaches the real bounded RunOnce")

    configured_bytes = configured_words * SAMPLE_STACK_BYTES_PER_WORD
    runtime_reserve = (
        configured_bytes - depth_bytes - SAMPLE_STACK_CONTEXT_RESERVE_BYTES
    )
    check(runtime_reserve >= SAMPLE_STACK_RUNTIME_RESERVE_BYTES,
          "Sample stack retains 64-byte Cortex-M3 context reserve plus "
          "at least 192 bytes runtime margin")
    info(
        f"Sample stack words={configured_words} bytes={configured_bytes} "
        f"static_max_depth={depth_bytes} "
        f"context_reserve={SAMPLE_STACK_CONTEXT_RESERVE_BYTES} "
        f"runtime_margin={runtime_reserve}"
    )
    return evidence_path


def parse_test_size(build_log: str, image: str) -> tuple[int, int, int, int] | None:
    section_match = re.search(
        rf"SIZE_REPORT_BEGIN {re.escape(image)}(.*?)"
        rf"SIZE_REPORT_END {re.escape(image)}",
        build_log,
        re.DOTALL,
    )
    if section_match is None:
        return None
    totals = re.search(
        r"^\s*(\d+)\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+"
        r"Grand Totals\s*$",
        section_match.group(1),
        re.MULTILINE,
    )
    if totals is None:
        return None
    return tuple(int(value) for value in totals.groups())


def verify_phase9_boundary(production_map: str) -> None:
    app_rtos = load(APP / "app_rtos.c")
    task_state = function_body(app_rtos, "void Task_State(void *argument)")
    check(bool(task_state), "Task_State production entry remains present")
    without_comments = re.sub(
        r"/\*.*?\*/|//[^\r\n]*", "", task_state, flags=re.DOTALL
    )
    calls = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", without_comments))
    calls.difference_update({"for", "if", "while", "switch", "sizeof"})
    expected_calls = {
        "Task_State", "pdMS_TO_TICKS", "xTaskGetTickCount",
        "vTaskDelayUntil",
    }
    check(calls == expected_calls,
          "Task_State remains a delay-only placeholder with no Phase 9 calls")
    forbidden_task_tokens = (
        "BMS_Data_", "BMS_Fault_", "BMS_Protect_", "BQ76940_",
        "g_bms_fet_request", "xEventGroupSetBits", "IWDG", "Watchdog",
    )
    check(all(token not in without_comments for token in forbidden_task_tokens),
          "Task_State performs no state/protection/FET/IWDG work")

    state_size = linked_thumb_size(production_map, "Task_State")
    check(state_size is not None and state_size <= 32,
          "production Task_State stays within the placeholder code-size bound")
    state_refs = set(re.findall(
        r"^\s*app_rtos\.o\(i\.Task_State\) refers to .* for "
        r"([A-Za-z_]\w*)\s*$",
        production_map,
        re.MULTILINE,
    ))
    check(state_refs == {"xTaskGetTickCount", "xTaskDelayUntil"},
          "production map binds Task_State only to tick/delay primitives")

    allowed_boundary_symbols = {
        "BMS_Protect_Decide",
        "BMS_Protect_Drain",
        "BMS_Protect_GetLatestCc",
        "BMS_Protect_GetXreadyState",
        "BMS_Protect_HandleCcReady",
        "BMS_Protect_HasFaultBits",
        "BMS_Protect_Init",
        "BMS_Protect_PushCcSample",
        "BMS_Protect_RecordAfeFailure",
        "BMS_Protect_RecordAfeReadSuccess",
        "BMS_Protect_RecordCcOverflow",
        "BMS_Protect_RecordW1cFinalizationAmbiguity",
        "BMS_Protect_RecoverXready",
        "BMS_Protect_ResolveObservedLowW1c",
        "BMS_Protect_ServicePending",
        "BMS_Protect_SetDevice",
        "BMS_Protect_XreadyBindingIsCurrent",
        "Task_Protect",
        "Task_State",
        "vTaskInternalSetTimeOutState",
        "xTaskGetSchedulerState",
    }
    boundary_symbols = {
        symbol for symbol in linked_thumb_symbols(production_map)
        if re.search(
            r"state|protect|protection|fet|iwdg|watchdog|manager",
            symbol,
            re.IGNORECASE,
        )
    }
    unexpected = sorted(boundary_symbols - allowed_boundary_symbols)
    check(not unexpected,
          "production map contains no new Phase 9 state/protection/"
          "FET-manager/IWDG symbols" +
          (f" (unexpected: {', '.join(unexpected)})" if unexpected else ""))


def verify_build_and_execution(started_ns: int,
                               production_sources: list[Path]) -> None:
    build_log_path = BUILD / "phase8_build.log"
    build_log = load(build_log_path)
    check(build_log.count(
        "Component: ARM Compiler 5.06 update 7 (build 960)") >= 4,
          "all direct build tools identify ARMCC5 5.06u7 build 960")
    check("UVISION_FILE_VERSION=5.38.0.0" in build_log,
          "gate records Keil uVision 5.38.0.0")
    check("Warning:" not in build_log and "Error:" not in build_log,
          "all six direct ARMCC5 test builds are warning/error free")
    for image in IMAGE_NAMES:
        check(f"TEST_IMAGE_BUILD_PASS {image}" in build_log,
              f"ARMCC5 compile/link completed: {image}")
        sizes = parse_test_size(build_log, image)
        check(sizes is not None, f"size report parses: {image}")
        if sizes is not None:
            code, ro, rw, zi = sizes
            check(code + ro + rw <= 64 * 1024,
                  f"test image ROM fits 64 KiB scatter: {image}")
            check(rw + zi <= 20 * 1024,
                  f"test image RAM fits 20 KiB scatter: {image}")
            info(f"{image} sizes Code={code} RO={ro} RW={rw} ZI={zi}")
    for image in ("phase8_data_tests", "phase8_sample_tests",
                  "phase8_afe_tests"):
        check(f"LANGUAGE_MODE {image} --c90" in build_log,
              f"Phase 8 production-C image uses ARMCC5 C90: {image}")

    manifest_inputs = verify_manifest(build_log, production_sources)
    verify_uvoptx_patch_evidence(build_log)

    map_requirements = {
        "phase4_regression_tests": (
            "BQ76940_ReadCellVoltages13", "BQ76940_ReadCcRaw",
            "Test_Phase4_Measurement",
        ),
        "phase6_regression_tests": (
            "App_Rtos_CreateObjects", "App_Rtos_CreateTasks",
            "Test_Phase6_Objects", "Test_Phase6_Tasks",
        ),
        "phase7_regression_tests": (
            "BMS_Protect_Drain", "BMS_Protect_ServicePending",
            "BMS_Protect_GetLatestCc", "BMS_Protect_GetXreadyState",
            "BMS_Protect_XreadyBindingIsCurrent", "Task_Protect",
            "Test_Phase7_BoundaryContracts",
        ),
        "phase8_data_tests": (
            "BMS_Data_PublishMeasurement", "BMS_Data_GetSnapshot",
            "BMS_Data_GetFreshnessSnapshot",
            "BMS_Data_IsFresh", "BMS_Ntc_ValidateTable",
            "BMS_Ntc_Interpolate", "Test_Phase8_Data",
        ),
        "phase8_sample_tests": (
            "BMS_Sample_RunOnce",
            "BMS_Sample_TestSeedConfigurationRevision",
            "BMS_Data_PublishMeasurement",
            "BMS_Data_GetFreshnessSnapshot",
            "BMS_Ntc_Interpolate", "BMS_Protect_GetLatestCc",
            "BMS_Protect_GetXreadyState",
            "BMS_Protect_XreadyBindingIsCurrent",
            "TestSample_ConfigurationRevisionGuard",
            "TestSample_ConfigurationRevisionImmediateWrapGuard",
            "Test_Phase8_Sample",
        ),
        "phase8_afe_tests": (
            "BMS_AfeStartup_Init", "BMS_AfeStartup_Step",
            "BMS_AfeStartup_IsFetOffConfirmed",
            "BQ76940_Control_ComposeProtect1",
            "BQ76940_Control_ComposeProtect2",
            "BQ76940_Control_ComposeProtect3",
            "BQ76940_Control_EncodeOvTrip",
            "BQ76940_Control_EncodeUvTrip",
            "BQ76940_Control_SysCtrl2WithFets",
            "Test_Phase8_AfeStartup",
        ),
    }
    evidence_paths: list[Path] = [build_log_path]
    for image, symbols in map_requirements.items():
        map_path = BUILD / f"{image}.map"
        axf_path = BUILD / f"{image}.axf"
        map_text = load(map_path)
        evidence_paths.extend((map_path, axf_path))
        check(axf_path.is_file(), f"test AXF exists: {image}")
        check("Component: ARM Compiler 5.06 update 7 (build 960)" in
              map_text, f"test map identifies ARMCC5 5.06u7: {image}")
        for symbol in symbols:
            check(linked_thumb_symbol(map_text, symbol),
                  f"{image} links production-C symbol {symbol}")

    simulator_path = BUILD / "phase8_simulator.log"
    simulator = load(simulator_path)
    evidence_paths.append(simulator_path)
    expected = (
        "PHASE4_REGRESSION_COMPLETED=1",
        "PHASE4_REGRESSION_FAILURES=0",
        "P4_MAPPING_FAILURES=0", "P4_MEASUREMENT_FAILURES=0",
        "P4_WRITE_COMMIT_FAILURES=0",
        "PHASE6_REGRESSION_COMPLETED=1",
        "PHASE6_REGRESSION_FAILURES=0", "P6_OBJECTS_FAILURES=0",
        "P6_TASKS_FAILURES=0", "P6_PROBE=4",
        "PHASE7_REGRESSION_COMPLETED=1",
        "PHASE7_REGRESSION_FAILURES=0", "P5_TRIP_FAILURES=0",
        "P5_OCDSCD_FAILURES=0", "P5_FET_FAILURES=0",
        "P5_CELLBAL_FAILURES=0", "P7_PROTECT_FAILURES=0",
        "P7_CC_FAILURES=0", "P7_RETRY_FAILURES=0",
        "P7_XREADY_FAILURES=0", "P7_BOUNDARY_FAILURES=0", "P7_PROBE=8",
        "PHASE8_DATA_TEST_COMPLETED=1", "PHASE8_DATA_TEST_FAILURES=0",
        "PHASE8_DATA_TEST_SUITE_ID=1", "PHASE8_DATA_TEST_PROBE=3",
        "P8_DATA_SUITE_COMPLETED=1", "P8_DATA_SUITE_FAILURES=0",
        "PHASE8_SAMPLE_TEST_COMPLETED=1", "PHASE8_SAMPLE_TEST_FAILURES=0",
        "PHASE8_SAMPLE_TEST_SUITE_ID=2", "PHASE8_SAMPLE_TEST_PROBE=3",
        "P8_SAMPLE_SUITE_COMPLETED=1", "P8_SAMPLE_SUITE_FAILURES=0",
        "P8_SAMPLE_CONTENTION_COMPLETED=1",
        "P8_SAMPLE_CONTENTION_ATTEMPTS=2",
        "P8_SAMPLE_CONTENTION_TIMEOUTS=1",
        "P8_SAMPLE_CONTENTION_SUCCESSES=1",
        "P8_SAMPLE_CONTENTION_GIVES=1",
        "P8_SAMPLE_CONTENTION_WAIT_TICKS=20",
        "P8_SAMPLE_CONTENTION_FIRST_TAKE_ORDER=6",
        "P8_SAMPLE_CONTENTION_CELL_GIVE_ORDER=7",
        "P8_SAMPLE_CONTENTION_RETRY_TAKE_ORDER=8",
        "P8_SAMPLE_CONTENTION_RETRY_GIVE_ORDER=9",
        "P8_SAMPLE_CONTENTION_PACK_CALL_ORDER=11",
        "P8_SAMPLE_XREADY_GUARD_COMPLETED=1",
        "P8_SAMPLE_XREADY_CELL_REJECTS=1",
        "P8_SAMPLE_XREADY_PACK_REJECTS=1",
        "P8_SAMPLE_XREADY_WRAP_REJECTS=1",
        "P8_SAMPLE_XREADY_ATOMIC_PUBLISHES=1",
        "PHASE8_AFE_TEST_COMPLETED=1", "PHASE8_AFE_TEST_FAILURES=0",
        "PHASE8_AFE_TEST_SUITE_ID=3", "PHASE8_AFE_TEST_PROBE=3",
        "P8_AFE_SUITE_COMPLETED=1", "P8_AFE_SUITE_FAILURES=0",
    )
    for token in expected:
        check(token in simulator, f"Keil Simulator evidence contains {token}")

    production_log_path = (
        PROJECT_DIR / "Build" / "BMS_V1_Phase8_build.log"
    )
    production_map_path = BUILD / "BMS_V1_Phase8_production.map"
    production_stack_path = verify_sample_stack_evidence(build_log)
    production_log = load(production_log_path)
    production_map = load(production_map_path)
    evidence_paths.extend((production_log_path, production_map_path,
                           production_stack_path))
    check("V5.06 update 7 (build 960)" in production_log,
          "production Clean/Rebuild used ARMCC5 5.06u7 build 960")
    check("0 Error(s), 0 Warning(s)" in production_log,
          "production Clean/Rebuild completed with zero errors/warnings")
    size_match = re.search(
        r"Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)",
        production_log,
    )
    check(size_match is not None, "production build reports Code/RO/RW/ZI")
    production_sizes: tuple[int, int, int, int] | None = None
    if size_match is not None:
        production_sizes = tuple(int(value) for value in size_match.groups())
        code, ro, rw, zi = production_sizes
        check(code + ro + rw <= 0xF400,
              "production ROM image fits configured 0xF400 IROM")
        check(rw + zi <= 20 * 1024,
              "production static/link-time RAM fits 20 KiB SRAM")
        info(f"production sizes Code={code} RO={ro} RW={rw} ZI={zi}")
    check(re.search(r"Load Region LR_IROM1 .*Max: 0x0000f400",
                    production_map) is not None,
          "production map enforces the 0xF400 IROM limit")
    check(re.search(r"Execution Region RW_IRAM1 .*Max: 0x00005000",
                    production_map) is not None,
          "production map enforces the 20 KiB IRAM limit")
    for symbol in (
        "Task_Sample", "BMS_Sample_RunOnce", "BMS_Data_PublishMeasurement",
        "BMS_Data_GetFreshnessSnapshot",
        "BMS_Ntc_Interpolate",
        "BMS_Protect_GetLatestCc", "BMS_Protect_GetXreadyState",
        "BMS_Protect_XreadyBindingIsCurrent",
    ):
        check(linked_thumb_symbol(production_map, symbol),
              f"production map links real {symbol}")
    check(not linked_thumb_symbol(
              production_map, "BMS_Sample_TestSeedConfigurationRevision"
          ) and
          "BMS_Sample_TestSeedConfigurationRevision" not in production_map,
          "production map contains no Phase 8 configuration seed seam")
    check("compiling bms_afe_startup.c" in production_log.lower() and
          "bms_afe_startup.o" in production_map.lower(),
          "production rebuild compiles and map-records the declared AFE object")
    unlinked_phase8_integration = (
        ("BMS_AfeStartup_Step", "bms_afe_startup.o"),
        ("BMS_Sample_SetCalibration", "bms_sample.o"),
        ("BMS_Sample_SetNtcTable", "bms_sample.o"),
    )
    for symbol, object_name in unlinked_phase8_integration:
        removed = re.search(
            rf"^\s*Removing {re.escape(object_name)}\(i\."
            rf"{re.escape(symbol)}\),",
            production_map,
            re.MULTILINE,
        ) is not None
        check(not linked_thumb_symbol(production_map, symbol) and removed,
              f"production map records current integration gap: {symbol} "
              "is removed/not linked")
    verify_phase9_boundary(production_map)
    totals = re.search(
        r"^\s*(\d+)\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+"
        r"Grand Totals\s*$",
        production_map,
        re.MULTILINE,
    )
    check(totals is not None, "production linker map reports grand totals")
    if totals is not None and production_sizes is not None:
        map_sizes = tuple(int(value) for value in totals.groups())
        check(map_sizes == production_sizes,
              "production build-log sizes agree with linker map")

    newest_manifest = max(
        (path.stat().st_mtime_ns for path in manifest_inputs if path.is_file()),
        default=0,
    )
    for path in evidence_paths:
        if not path.is_file():
            continue
        check(path.stat().st_mtime_ns >= started_ns,
              f"evidence belongs to this gate run: {path.relative_to(REPO)}")
        check(path.stat().st_mtime_ns >= newest_manifest,
              f"evidence is no older than declared inputs: {path.relative_to(REPO)}")


def verify_gate_decision() -> None:
    main = load(FW / "User" / "main.c")
    main_body = function_body(main, "int main(void)")
    main_without_comments = re.sub(
        r"/\*.*?\*/|//[^\r\n]*", "", main_body, flags=re.DOTALL
    )
    production_sources = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for root in (APP, FW / "User", FW / "Config")
        for path in root.glob("*.[ch]")
    )
    has_production_curve = re.search(
        r"(?:static\s+)?(?:const\s+)?BMS_NtcPoint_t\s+\w+\s*\[",
        production_sources,
    ) is not None
    check(not has_production_curve,
          "current production sources contain no embedded NTC point table")
    integration_calls = (
        "BMS_AfeStartup_Init(",
        "BMS_AfeStartup_Step(",
        "BMS_Sample_SetCalibration(",
        "BMS_Sample_SetNtcTable(",
    )
    check(all(call not in main_without_comments for call in integration_calls),
          "current main does not connect AFE startup, calibration or NTC table")

    # These are revision-locked approval gates, not source-discovery
    # heuristics. No newly added table, policy-looking identifier or call can
    # make this verifier silently approve a safety policy. A future gate must
    # be intentionally revised to bind the exact user-approved immutable
    # artifact revision and its approval evidence.
    block(
        f"UNCONDITIONAL {GATE_POLICY_REVISION}: no user-approved immutable "
        "production NTC table revision is bound; update a future gate "
        "revision explicitly after user approval"
    )
    block(
        f"UNCONDITIONAL {GATE_POLICY_REVISION}: no user-approved immutable "
        "AFE startup/PROTECT3 policy revision is bound; update a future gate "
        "revision explicitly after user approval"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--started-utc", required=True)
    args = parser.parse_args()
    try:
        started_ns = parse_started_ns(args.started_utc)
    except (TypeError, ValueError) as exc:
        check(False, f"--started-utc is a valid ISO-8601 timestamp ({exc})")
        started_ns = 0

    try:
        production_sources, _ = verify_project()
        verify_data_and_ntc_contracts()
        verify_sample_and_mailbox_contracts()
        verify_afe_contracts()
        verify_build_and_execution(started_ns, production_sources)
        verify_gate_decision()
    except OSError as exc:
        check(False, f"verification filesystem error: {exc}")
    except Exception as exc:  # Fail closed on a verifier defect.
        check(False, f"verification internal error: {type(exc).__name__}: {exc}")

    if failures:
        results.append(
            f"RESULT: PHASE8 TEST/EVIDENCE FAIL ({len(failures)} failure(s)); "
            "HARD GATE FAIL"
        )
        exit_code = 1
    elif blockers:
        results.append("RESULT: PHASE8 TEST/EVIDENCE PASS")
        results.append(
            f"RESULT: PHASE8 HARD GATE BLOCKED ({len(blockers)} blocker(s))"
        )
        exit_code = 2
    else:
        results.append("RESULT: PHASE8 TEST/EVIDENCE PASS")
        results.append("RESULT: PHASE8 HARD GATE PASS")
        exit_code = 0

    BUILD.mkdir(parents=True, exist_ok=True)
    output = "\n".join(results) + "\n"
    VERIFY_LOG.write_text(output, encoding="utf-8")
    sys.stdout.write(output)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
