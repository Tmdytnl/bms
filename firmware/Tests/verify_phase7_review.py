#!/usr/bin/env python3
"""Verify the reproducible Codex Phase 7 reviewed-candidate evidence."""

from __future__ import annotations

import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
FW = REPO / "firmware"
TESTS = FW / "Tests"
BUILD = TESTS / "Build" / "Phase7Review"
PROJECT_DIR = FW / "Project" / "Keil"
VERIFY_LOG = BUILD / "verify_phase7_review.log"


class VerificationError(RuntimeError):
    pass


results: list[str] = []


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)
    results.append(f"PASS: {message}")


def read(path: Path) -> str:
    require(path.is_file(), f"artifact exists: {path.relative_to(REPO)}")
    return path.read_text(encoding="utf-8", errors="replace")


def require_fresh(output: Path, inputs: list[Path], label: str) -> None:
    require(output.is_file(), f"{label} exists")
    missing = [path for path in inputs if not path.is_file()]
    require(not missing, f"{label} inputs exist")
    newest_input = max(path.stat().st_mtime_ns for path in inputs)
    require(output.stat().st_mtime_ns >= newest_input,
            f"{label} is fresh relative to its sources")


def map_defines_code_symbol(map_text: str, symbol: str) -> bool:
    """Reject unused-section mentions; require a linked Thumb definition."""
    return re.search(
        rf"^\s*{re.escape(symbol)}\s+0x[0-9a-f]+\s+Thumb Code\s+\d+\b",
        map_text,
        re.IGNORECASE | re.MULTILINE,
    ) is not None


def headers_under(*roots: Path) -> list[Path]:
    """Return a conservative header dependency set for ARMCC5 images."""
    headers: list[Path] = []
    for root in roots:
        headers.extend(root.rglob("*.h"))
    return sorted(set(headers))


COMMON_HEADERS = headers_under(
    FW / "App",
    FW / "Config",
    FW / "Driver",
    TESTS,
    REPO / "docs" / "FreeRTOS",
    REPO / "docs" / "reference" / "ST" /
    "STM32F10x Standard Peripheral Library",
)

STARTUP_SOURCE = (
    REPO / "docs" / "reference" / "ST" /
    "STM32F10x Standard Peripheral Library" / "Start" /
    "startup_stm32f10x_md.s"
)


def verify_project() -> None:
    project_path = PROJECT_DIR / "BMS_V1.uvprojx"
    root = ET.parse(project_path).getroot()
    file_paths = [node.text for node in root.findall(".//FilePath") if node.text]
    require(len(file_paths) == 32, "production Keil target contains 32 files")
    missing: list[str] = []
    for value in file_paths:
        candidate = (PROJECT_DIR / value.replace("\\", os.sep)).resolve()
        if not candidate.is_file():
            missing.append(value)
    require(not missing, "all production Keil FilePath entries resolve")

    options = read(PROJECT_DIR / "BMS_V1.uvoptx")
    require("..\\..\\Tests\\phase7_review_simulator.ini" in options,
            "Keil Simulator entry is repository-relative")
    require("D:\\AI\\Codex\\Bms_shop" not in options,
            "active Keil Simulator entry has no stale workspace path")


def verify_source_contracts() -> None:
    main = read(FW / "User" / "main.c")
    protect = read(FW / "App" / "bms_protect.c")
    protect_h = read(FW / "App" / "bms_protect.h")
    fault_h = read(FW / "App" / "bms_fault.h")
    rtos_h = read(FW / "App" / "app_rtos.h")
    rtos_hooks = read(FW / "App" / "app_rtos_hooks.c")
    exti = read(FW / "Driver" / "bsp_exti.c")
    transport = read(FW / "Driver" / "bq76940.c")
    transport_h = read(FW / "Driver" / "bq76940.h")
    control = read(FW / "Driver" / "bq76940_control.c")
    phase4_test = read(TESTS / "test_phase4_measurement.c")
    phase7_test = read(TESTS / "test_phase7_logic.c")
    boundary_report = read(
        REPO / "deliverables" / "review" / "BMS_V1_Codex_Phase7_Review.md"
    )

    objects_pos = main.find("App_Rtos_CreateObjects()")
    tasks_pos = main.find("App_Rtos_CreateTasks()")
    scheduler_pos = main.find("vTaskStartScheduler()")
    require(0 <= objects_pos < tasks_pos < scheduler_pos,
            "startup creates RTOS objects/tasks before scheduler start")
    require("BSP_ALERT_EXTI_Init()" not in main and
            "BSP_ALERT_PinActive()" not in main,
            "main never enables ALERT before FreeRTOS port initialization")
    task_start = protect.find("void Task_Protect")
    task_source = protect[task_start:]
    task_exti_pos = task_source.find("BSP_ALERT_EXTI_Init()")
    task_seed_pos = task_source.find("retry_pending = BSP_ALERT_PinActive()")
    task_loop_pos = task_source.find("for (;;)")
    require(task_start >= 0 and
            0 <= task_exti_pos < task_seed_pos < task_loop_pos,
            "ProtectTask enables EXTI after scheduler start and seeds high pin")
    require("EXTI_ClearITPendingBit(EXTI_Line1);" in exti,
            "EXTI init clears a stale controller pending latch")
    require("taskDISABLE_INTERRUPTS();" in rtos_hooks and
            "__disable_irq();" in main,
            "fatal RTOS hooks and main safe-idle disable interrupts")

    require("BMS_Protect_ServicePending" in protect and
            "BSP_ALERT_PinActive()" in protect and
            "vTaskDelay(pdMS_TO_TICKS(BMS_PROTECT_RETRY_DELAY_MS))" in protect,
            "ProtectTask retains delayed task-level pending retry")
    require("BMS_PROTECT_DRAIN_MAX_ITER" in protect and
            "BMS_PROTECT_DRAIN_RETRY_REQUIRED" in protect,
            "SYS_STAT drain has a bounded per-attempt budget and retry result")
    require(protect.find("BMS_Protect_Decide(stat") <
            protect.find("BMS_Protect_HandleCcReady(device"),
            "CC_READY clear bit is added after fault decision reset")
    require("s_cc_clear_pending" in protect,
            "CC W1C retry cannot enqueue one hardware sample twice")
    ambiguous_status = "BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS"
    require(all(ambiguous_status in source for source in
                (transport_h, transport, phase4_test, protect, phase7_test)),
            "ACKed write plus failed STOP is finalization-ambiguous end-to-end")
    require("BQ76940_STATUS_WRITE_ACCEPTED_STOP_ERROR" not in "\n".join(
                (transport_h, transport, phase4_test, protect, phase7_test)),
            "no executable contract claims ACKed W1C is definitely committed")
    require(all(token in protect_h for token in (
                "w1c_finalization_ambiguous_count",
                "cc_event_identity_ambiguous_count",
                "w1c_finalization_ambiguous_mask",
                "w1c_finalization_ambiguous_latched")),
            "W1C finalization ambiguity has observable counter and state")
    require("BMS_Protect_RecordW1cFinalizationAmbiguity" in protect and
            "BMS_Protect_ResolveObservedLowW1c" in protect and
            "~s_w1c_finalization_ambiguous_mask" in protect,
            "ambiguous W1C bits are quarantined until observed low")
    require("cc_event_identity_ambiguous_count == 1UL" in phase7_test and
            "TestP7_CcReadCount() == 1U" in phase7_test and
            "TestP7_WriteCount() == 1U" in phase7_test,
            "production-C regression forbids ambiguous CC replay/reenqueue")

    require("EVT_CC_QUEUE_OVERFLOW" in rtos_h and
            "cc_queue_overflow_count" in protect_h and
            "cc_sample_missed_count" in protect_h and
            "cc_enqueue_failure_count" in protect_h,
            "CC overflow has observable event and exact counters")
    require("vTaskSuspendAll();" in protect and "xTaskResumeAll();" in protect,
            "drop-oldest plus enqueue-newest is scheduler-protected")
    require("TestP7_SetReplacementFailures(1U)" in phase7_test,
            "CC replacement failure regression is executable")
    require("TestP7_RunProtectTaskRetryScenario()" in phase7_test and
            "TestP7_RunProtectTaskAlreadyHighScenario()" in phase7_test and
            "TestP7_ExerciseAlertIsr()" in phase7_test,
            "H-05 regression executes task retry, startup-high seed and ISR")

    require("s_xready_recovery_hook" in protect and
            "BMS_PROTECT_STAT_DEVICE_XREADY" in protect,
            "XREADY clear is gated by an authoritative recovery hook")
    require("4250U" not in protect and "2800U" not in protect,
            "XREADY recovery no longer overwrites authoritative configuration")

    getter_start = protect.find("BMS_FaultSummary_t BMS_Protect_GetFaultSummary")
    getter_end = protect.find("BMS_ProtectDiagnostics_t", getter_start)
    getter = protect[getter_start:getter_end]
    require(getter_start >= 0 and
            0 <= getter.find("vTaskSuspendAll();") <
            getter.find("snapshot = s_fault;") <
            getter.find("xTaskResumeAll();"),
            "fault getter copies active+latched under scheduler protection")
    drain_decide = protect.find("BMS_Protect_Decide(stat")
    require(protect.rfind("vTaskSuspendAll();", 0, drain_decide) >= 0 and
            protect.find("xTaskResumeAll();", drain_decide) >= 0,
            "ProtectTask publishes active+latched under scheduler protection")
    require("cleared hardware status bit alone is not proof" in fault_h and
            "SYS_STAT=0" in protect_h and
            "BMS_Protect_Decide(0U" in phase7_test,
            "fault API and production-C regression forbid W1C-as-recovery")
    require(all(token in phase7_test for token in (
                "!BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_OV)",
                "!BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_UV)",
                "!BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_OCD)",
                "BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_SCD)",
                "BMS_FAULT_ID_AFE_OVRD_ALERT")),
            "Phase 7 recovery-eligible versus latched fault policy is tested")
    require("HARDWARE VALIDATION REQUIRED: BQ7694003 W1C commit behavior "
            "when STOP finalization fails" in boundary_report,
            "report preserves the mandatory W1C hardware-validation boundary")

    require("44U, 67U, 89U, 111U, 133U, 155U, 178U, 200U" in control,
            "SCD RSNS=1 table matches TI Rev. I")
    normalized_control = re.sub(r"\s+", " ", control)
    require(
        "microvolts = ((int64_t)full_code * "
        "calibration->gain_uv_per_lsb) + "
        "((int64_t)calibration->offset_mv * 1000LL);" in normalized_control,
            "OV/UV decode applies calibration offset with the correct sign")
    require("current_ctrl2 & 0x40U" in control and
            "current_ctrl2 & 0xC0U" not in control,
            "SYS_CTRL2 RMW preserves only CC_EN in production")
    composer_guards = (
        "delay_code >= BQ76940_CONTROL_SCD_DELAY_COUNT",
        "thresh_code >= BQ76940_CONTROL_SCD_THRESHOLD_COUNT",
        "delay_code >= BQ76940_CONTROL_OCD_DELAY_COUNT",
        "thresh_code >= BQ76940_CONTROL_OCD_THRESHOLD_COUNT",
        "uv_delay_code >= BQ76940_CONTROL_UV_DELAY_COUNT",
        "ov_delay_code >= BQ76940_CONTROL_OV_DELAY_COUNT",
    )
    require(all(guard in control for guard in composer_guards),
            "Phase 5 selectors/composers reject out-of-range configuration")

    production_sources = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in (FW / "App").glob("*.c")
    ) + "\n" + "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in (FW / "User").glob("*.c")
    )
    require("BQ76940_REG_SYS_CTRL2" not in production_sources,
            "no Phase 4-7 app module bypasses the FET arbitration foundation")


def verify_build_and_execution() -> None:
    production_log_path = PROJECT_DIR / "Build" / "BMS_V1_Codex_Phase7_build.log"
    production_map_path = PROJECT_DIR / "Listings" / "BMS_V1.map"
    production_log = read(production_log_path)
    require("V5.06 update 7 (build 960)" in production_log,
            "production rebuild used ARMCC5 5.06u7 build 960")
    require("0 Error(s), 0 Warning(s)" in production_log,
            "production Clean/Rebuild has zero errors and zero warnings")
    match = re.search(
        r"Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)",
        production_log,
    )
    require(match is not None, "production build reports Code/RO/RW/ZI")
    assert match is not None
    code, ro, rw, zi = (int(value) for value in match.groups())
    require(code + ro + rw <= 0xF400,
            "production ROM image fits configured STM32F103C8 IROM")
    require(rw + zi <= 20 * 1024,
            "production static/link-time RAM fits 20 KiB SRAM")
    results.append(f"INFO: production sizes Code={code} RO={ro} RW={rw} ZI={zi}")

    production_map = read(production_map_path)
    require("Component: ARM Compiler 5.06 update 7 (build 960)" in
            production_map,
            "production map identifies ARMCC5 5.06u7 build 960")
    require(re.search(
        r"Load Region LR_IROM1 .*Max: 0x0000f400", production_map
    ) is not None, "production map enforces the 0xF400 IROM limit")
    require(re.search(
        r"Execution Region RW_IRAM1 .*Max: 0x00005000", production_map
    ) is not None, "production map enforces the 20 KiB IRAM limit")
    for symbol in (
        "Task_Protect",
        "EXTI1_IRQHandler",
        "BMS_Protect_Drain",
        "BMS_Protect_ServicePending",
        "BMS_Protect_PushCcSample",
        "BSP_ALERT_EXTI_Init",
        "BSP_ALERT_PinActive",
        "BQ76940_ReadCcRaw",
        "vTaskDelay",
        "vTaskSuspendAll",
        "xEventGroupSetBits",
    ):
        require(map_defines_code_symbol(production_map, symbol),
                f"production map links the real {symbol} path")
    totals = re.search(
        r"^\s*(\d+)\s+\d+\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+Grand Totals\s*$",
        production_map,
        re.MULTILINE,
    )
    require(totals is not None, "production map reports grand totals")
    assert totals is not None
    require(tuple(int(value) for value in totals.groups()) ==
            (code, ro, rw, zi),
            "production log sizes agree with the linker map")

    build_log = read(BUILD / "phase7_review_build.log")
    for tool in ("armcc", "armasm", "armlink", "fromelf"):
        require(f"Tool: {tool}" in build_log,
                f"review build records the {tool} version")
    require(build_log.count(
        "Component: ARM Compiler 5.06 update 7 (build 960)") >= 4,
        "all review build tools identify ARMCC5 5.06u7 build 960")
    require("uVision FileVersion: 5.38.0.0" in build_log,
            "review gate records Keil uVision 5.38")
    for suite in ("phase4", "phase6", "phase7"):
        require(f"{suite} TEST IMAGE BUILD: PASS" in build_log,
                f"{suite} ARMCC5 test image built and linked")
    require("Warning:" not in build_log and "Error:" not in build_log,
            "review test-image build has no compiler/linker warnings or errors")

    expected_simulator = (
        "PHASE4_REVIEW_TEST_COMPLETED=1",
        "PHASE4_REVIEW_TEST_FAILURES=0",
        "P4_MAPPING_FAILURES=0",
        "P4_MEASUREMENT_FAILURES=0",
        "P4_WRITE_COMMIT_FAILURES=0",
        "PHASE6_REVIEW_TEST_COMPLETED=1",
        "PHASE6_REVIEW_TEST_FAILURES=0",
        "P6_OBJECTS_FAILURES=0",
        "P6_TASKS_FAILURES=0",
        "P6_PROBE=4",
        "PHASE7_REVIEW_TEST_COMPLETED=1",
        "PHASE7_REVIEW_TEST_FAILURES=0",
        "P5_TRIP_FAILURES=0",
        "P5_OCDSCD_FAILURES=0",
        "P5_FET_FAILURES=0",
        "P5_CELLBAL_FAILURES=0",
        "P7_PROTECT_FAILURES=0",
        "P7_CC_FAILURES=0",
        "P7_RETRY_FAILURES=0",
        "P7_XREADY_FAILURES=0",
        "P7_BOUNDARY_FAILURES=0",
        "P7_PROBE=8",
    )
    simulator_log = read(BUILD / "phase7_review_simulator.log")
    for token in expected_simulator:
        require(token in simulator_log, f"Simulator evidence contains {token}")

    map_requirements = {
        "phase4_review_tests.map": (
            "BQ76940_ReadCellVoltages13",
            "BQ76940_ReadCcRaw",
            "Test_Phase4_Measurement",
        ),
        "phase6_review_tests.map": (
            "App_Rtos_CreateObjects",
            "App_Rtos_CreateTasks",
            "Test_Phase6_Objects",
            "Test_Phase6_Tasks",
        ),
        "phase7_review_tests.map": (
            "BQ76940_Control_ComposeProtect1",
            "BQ76940_Control_SysCtrl2WithFets",
            "BMS_Protect_Drain",
            "BMS_Protect_ServicePending",
            "BMS_Protect_PushCcSample",
            "BMS_Protect_RecoverXready",
            "Task_Protect",
            "EXTI1_IRQHandler",
            "BSP_ALERT_EXTI_Init",
            "Test_Phase7_AlertRetry",
            "Test_Phase7_CcQueue",
            "Test_Phase7_Xready",
            "Test_Phase7_BoundaryContracts",
            "BMS_Protect_GetFaultSummary",
        ),
    }
    for map_name, symbols in map_requirements.items():
        map_text = read(BUILD / map_name)
        require("Component: ARM Compiler 5.06 update 7 (build 960)" in
                map_text,
                f"{map_name} identifies ARMCC5 5.06u7 build 960")
        for symbol in symbols:
            require(map_defines_code_symbol(map_text, symbol),
                    f"{map_name} links {symbol}")

    phase4_inputs = COMMON_HEADERS + [
        STARTUP_SOURCE,
        TESTS / "phase4_tests.sct",
        TESTS / "build_phase7_review.ps1",
        FW / "Driver" / "bq76940.c",
        FW / "Driver" / "bq76940_measurement.c",
        FW / "Driver" / "crc8_bq76940.c",
        TESTS / "test_phase4_mapping.c",
        TESTS / "test_phase4_measurement.c",
        TESTS / "test_phase4_main.c",
    ]
    phase6_inputs = COMMON_HEADERS + [
        STARTUP_SOURCE,
        TESTS / "phase6_tests.sct",
        TESTS / "build_phase7_review.ps1",
        FW / "App" / "app_rtos.c",
        FW / "App" / "app_rtos_hooks.c",
        REPO / "docs" / "FreeRTOS" / "source" / "tasks.c",
        REPO / "docs" / "FreeRTOS" / "source" / "queue.c",
        REPO / "docs" / "FreeRTOS" / "source" / "list.c",
        REPO / "docs" / "FreeRTOS" / "source" / "event_groups.c",
        REPO / "docs" / "FreeRTOS" / "source" / "timers.c",
        REPO / "docs" / "FreeRTOS" / "portable" / "heap_4.c",
        REPO / "docs" / "FreeRTOS" / "portable" / "port.c",
        TESTS / "test_phase6_task_stub.c",
        TESTS / "test_phase6_objects.c",
        TESTS / "test_phase6_tasks.c",
        TESTS / "test_phase6_main.c",
    ]
    phase7_inputs = COMMON_HEADERS + [
        STARTUP_SOURCE,
        TESTS / "phase7_tests.sct",
        TESTS / "build_phase7_review.ps1",
        FW / "App" / "bms_protect.c",
        FW / "App" / "bms_fault.c",
        FW / "Driver" / "bq76940_control.c",
        TESTS / "test_phase7_stub_i2c.c",
        TESTS / "test_phase5_trip.c",
        TESTS / "test_phase5_ocdscd.c",
        TESTS / "test_phase5_fet.c",
        TESTS / "test_phase5_cellbal.c",
        TESTS / "test_phase7_logic.c",
        TESTS / "test_phase7_main.c",
    ]
    require_fresh(BUILD / "phase4_review_tests.axf", phase4_inputs,
                  "Phase 4 test AXF")
    require_fresh(BUILD / "phase6_review_tests.axf", phase6_inputs,
                  "Phase 6 test AXF")
    require_fresh(BUILD / "phase7_review_tests.axf", phase7_inputs,
                  "Phase 5/7 test AXF")
    require_fresh(BUILD / "phase4_review_tests.map", phase4_inputs,
                  "Phase 4 test map")
    require_fresh(BUILD / "phase6_review_tests.map", phase6_inputs,
                  "Phase 6 test map")
    require_fresh(BUILD / "phase7_review_tests.map", phase7_inputs,
                  "Phase 5/7 test map")
    require_fresh(BUILD / "phase7_review_build.log",
                  phase4_inputs + phase6_inputs + phase7_inputs + [
                      BUILD / "phase4_review_tests.axf",
                      BUILD / "phase6_review_tests.axf",
                      BUILD / "phase7_review_tests.axf",
                      BUILD / "phase4_review_tests.map",
                      BUILD / "phase6_review_tests.map",
                      BUILD / "phase7_review_tests.map",
                  ],
                  "review test build log")
    require_fresh(BUILD / "phase7_review_simulator.log",
                  [BUILD / "phase4_review_tests.axf",
                   BUILD / "phase6_review_tests.axf",
                   BUILD / "phase7_review_tests.axf",
                   TESTS / "phase7_review_simulator.ini",
                   PROJECT_DIR / "BMS_V1.uvprojx",
                   PROJECT_DIR / "BMS_V1.uvoptx"],
                  "review Simulator log")
    project_root = ET.parse(PROJECT_DIR / "BMS_V1.uvprojx").getroot()
    production_sources = [
        (PROJECT_DIR / node.text.replace("\\", os.sep)).resolve()
        for node in project_root.findall(".//FilePath") if node.text
    ]
    production_inputs = COMMON_HEADERS + production_sources + [
        PROJECT_DIR / "BMS_V1.uvprojx",
    ]
    require_fresh(production_log_path, production_inputs,
                  "production rebuild log")
    require_fresh(production_map_path, production_inputs,
                  "production linker map")


def verify_uart_status() -> None:
    uart_files = list(FW.rglob("bsp_uart.c")) + list(FW.rglob("bsp_uart.h"))
    require(not uart_files, "UART implementation is absent as reported")
    project = read(PROJECT_DIR / "BMS_V1.uvprojx")
    require("USART" not in project and "bsp_uart" not in project.lower(),
            "production target contains no USART/UART implementation")
    results.append("INFO: UART REQUIRED — IMPLEMENTATION MISSING")


def main() -> int:
    try:
        verify_project()
        verify_source_contracts()
        verify_build_and_execution()
        verify_uart_status()
        results.append("RESULT: PHASE7 REVIEW VERIFICATION PASS")
        exit_code = 0
    except (OSError, ET.ParseError, VerificationError) as exc:
        results.append(f"FAIL: {exc}")
        results.append("RESULT: PHASE7 REVIEW VERIFICATION FAIL")
        exit_code = 1

    BUILD.mkdir(parents=True, exist_ok=True)
    output = "\n".join(results) + "\n"
    VERIFY_LOG.write_text(output, encoding="utf-8")
    sys.stdout.write(output)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
