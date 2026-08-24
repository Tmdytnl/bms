#!/usr/bin/env python3
"""Independent/static Phase 6 gate checks; never invokes GCC.

Verifies independently of the C code under test:
  1. Phase 5 candidate + Phase 1..4 validated input hashes (regression).
  2. FreeRTOSConfig lock (errata C-01/C-02/H-08/H-09).
  3. RTOS objects and seven-task skeleton declarations.
  4. main integration: objects -> tasks -> vTaskStartScheduler.
  5. Phase 6 boundary: no ALERT/ProtectTask bodies, no CAN, no Flash.
  6. ARMCC5 Simulator log + test AXF freshness.
"""

from __future__ import annotations

import hashlib
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware"
PROJECT = FW / "Project" / "Keil" / "BMS_V1.uvprojx"
RTOS_H = FW / "App" / "app_rtos.h"
RTOS_C = FW / "App" / "app_rtos.c"
HOOKS_C = FW / "App" / "app_rtos_hooks.c"
CONFIG_H = FW / "Config" / "FreeRTOSConfig.h"
MAIN_C = FW / "User" / "main.c"
SIM_LOG = FW / "Tests" / "Build" / "Phase6" / "phase6_simulator.log"
TEST_AXF = FW / "Tests" / "Build" / "Phase6" / "phase6_tests.axf"
TEST_MAP = FW / "Tests" / "Build" / "Phase6" / "phase6_tests.map"
TEST_SOURCES = (
    FW / "App" / "app_rtos.c",
    FW / "App" / "app_rtos_hooks.c",
    FW / "Tests" / "test_phase6_main.c",
    FW / "Tests" / "test_phase6_objects.c",
    FW / "Tests" / "test_phase6_tasks.c",
)

PHASE5_UVPROJX_HASH = "c13d76bb7c3c47f58e030fab199a18ecc2387d8849dac640df6c745a3173f174"

EXPECTED_APP_HASHES = {
    "bms_data.c": "377afb18c92d9fc28e0957256348d676944e880c0bf15cd6386b1812950cc59a",
    "bms_data.h": "1e5e240514aaeb5ceee93f070f8d4febeea0e2d60a134ec8df099d4b5e663eac",
    "bms_fault.c": "de387a67aa496aaea2d3732ad81ab85933dba7016999df6789505b5487b7d28e",
    "bms_fault.h": "61df623e8173abbea7483763b24bbac95ab4cfb3738900d256395e233bca13f4",
    "bms_state.h": "be487ed78e62a03d018222095f0acdf5dddf79a2b0e42e81da35a8a48c845330",
    "bms_types.h": "69fbe5c6ecc0bd57284b326c85c793a5622c400273ff36520d5a05d4bab01306",
}

EXPECTED_PHASE2_HASHES = {
    "Config/bms_config.h": "66c487d799244e78d7c9330b6356ec17bdc08f04f85ec807cea3326c8ff75830",
    "Driver/bsp_clock.c": "97d19f3468faa2b9ade3517fda9ad06948697a72359d46d68bc6fa8cbdd44136",
    "Driver/bsp_clock.h": "408e97a1c6b7cde12bf1ad7271a4406dee095af4d99f243e4c2d5cbacc94dea1",
    "Driver/bsp_gpio.c": "334930106c7a296c34ed12183a27bd2df2bcb5fd509eed529278dba78470d0f7",
    "Driver/bsp_gpio.h": "8a6226b738a5429ca4c451624d45132012a71d6445fcdb8a343496d3bb229376",
    "Driver/bsp_timer.c": "696e8a03a9961b754d26b95d8fcf77f0f669e3765968d1d6a467a43727f8149a",
    "Driver/bsp_timer.h": "6a53249441a19b07b2af8bf93d01eea18c6c8e76d65cf4e20d35228078d9b14e",
    "Driver/soft_i2c.c": "ac4966426a0ebd56c5463523afc130ea2a9d3fdf0e2ec6b34092e0dd2b3afc2f",
    "Driver/soft_i2c.h": "aded2c24df34bcb2ba95c53ffb46dc69c8111d8b57161065d41e58d8bed1eaa0",
    "Driver/crc8_bq76940.c": "8dfeb70d18bb5226d3be0d47b99e0b0802e70770c2c79fe194d36cb3a9812976",
    "Driver/crc8_bq76940.h": "0a4b07e4f927805e482a797d9c61702201882031087c5d49941db72afd934e22",
}

EXPECTED_PHASE3_HASHES = {
    "Driver/bq76940.c": "d6ee9ebbf395bb3b68b9d4d90b600d2c0505c8969237eb0b4205c35191bb0a69",
    "Driver/bq76940.h": "af8aa66fd90a0cf29d1570879e7f4d8a18d369e0a9117343611f2bc8d4efbecb",
    "Driver/bq76940_regs.h": "0ef807d409fa68d97e713900e870ec429515a7a8bdfae3fc17747774855f602e",
}

EXPECTED_PHASE4_HASHES = {
    "Driver/bq76940_measurement.h": "4883401eef11bd8fca01f9c20d9607e785b844fe82321120fad154fc5355f877",
    "Driver/bq76940_measurement.c": "69819eacae2ebdea8c911397522cfb26ae03c1e09e38d85bbd33c81e3db4e177",
}

EXPECTED_PHASE5_HASHES = {
    "Driver/bq76940_control.h": "08c1cfdf5f22490005d8defc9e0512cd04393f15afae056c1ece16e9448260ca",
    "Driver/bq76940_control.c": "9cee4902205c1c967fddef5188f79f3c842ef59d74847fa3f5216bcaba8f8dfa",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_regression() -> None:
    # Phase 5 report hash is filled by the Phase 6 report author after the
    # Phase 5 report is finalized; the uvprojx is now extended by Phase 6.
    tree = ET.parse(PROJECT)
    paths = [node.text or "" for node in tree.findall(".//FilePath")]
    joined = "\n".join(paths).lower()
    for expected in (
        "bq76940.c", "crc8_bq76940.c", "soft_i2c.c", "bsp_clock.c",
        "bsp_gpio.c", "bsp_timer.c", "stm32f10x_rcc.c",
        "stm32f10x_gpio.c", "stm32f10x_tim.c", "startup_stm32f10x_md.s",
        "bq76940_measurement.c", "bq76940_control.c",
        "tasks.c", "queue.c", "list.c", "event_groups.c", "timers.c",
        "stream_buffer.c", "croutine.c", "heap_4.c", "port.c",
        "app_rtos.c", "app_rtos_hooks.c",
    ):
        require(expected in joined, f"target source missing {expected}")
    require("stm32f10x_i2c.c" not in joined, "hardware-I2C SPL source linked")

    # FreeRTOS include paths present.
    inc = tree.findtext(".//VariousControls/IncludePath") or ""
    require("docs\\FreeRTOS\\include" in inc or "docs/FreeRTOS/include" in inc,
            "FreeRTOS include path missing")

    for rel, expected in {**EXPECTED_APP_HASHES,
                          **EXPECTED_PHASE2_HASHES,
                          **EXPECTED_PHASE3_HASHES,
                          **EXPECTED_PHASE4_HASHES,
                          **EXPECTED_PHASE5_HASHES}.items():
        path = FW / "App" / rel if rel.startswith("bms_") else FW / rel
        require(sha256(path) == expected,
                f"input drifted: {rel}")
    print("PASS: Phase 1..5 input hashes + Phase 6 target diff")


def check_freertos_config() -> None:
    text = CONFIG_H.read_text(encoding="utf-8")

    expected = {
        "configUSE_PREEMPTION": "1",
        "configTICK_RATE_HZ": "(1000UL)",
        "configMAX_PRIORITIES": "(8)",
        "configTOTAL_HEAP_SIZE": "(12 * 1024)",
        "configPRIO_BITS": "4",
        "configLIBRARY_LOWEST_INTERRUPT_PRIORITY": "15",
        "configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY": "5",
        "configUSE_MUTEXES": "1",
        "configUSE_COUNTING_SEMAPHORES": "1",
        "configCHECK_FOR_STACK_OVERFLOW": "2",
        "configUSE_PORT_OPTIMISED_TASK_SELECTION": "1",
        "configUSE_TIMERS": "1",
    }
    for name, value in expected.items():
        require(re.search(rf"#define\s+{name}\s+{re.escape(value)}", text),
                f"FreeRTOSConfig {name} != {value}")

    # C-02 raw priorities.
    require(re.search(r"configKERNEL_INTERRUPT_PRIORITY.*0xF0|configKERNEL_INTERRUPT_PRIORITY.*15 <<", text),
            "kernel raw priority not 0xF0")
    require(re.search(r"configMAX_SYSCALL_INTERRUPT_PRIORITY.*0x50|configMAX_SYSCALL_INTERRUPT_PRIORITY.*5 <<", text),
            "max syscall raw priority not 0x50")

    # Handler mapping to startup vector names.
    for h in ("vPortSVCHandler", "xPortPendSVHandler", "xPortSysTickHandler"):
        require(h in text, f"handler mapping missing {h}")
    require("SVC_Handler" in text and "PendSV_Handler" in text and
            "SysTick_Handler" in text, "vector name mapping incomplete")

    # Diagnostics (H-09).
    require("configASSERT" in text, "configASSERT missing")
    require("vApplicationAssertFailedHandler" in text,
            "assert handler missing")
    require("configCHECK_FOR_STACK_OVERFLOW" in text,
            "stack overflow check missing")
    print("PASS: FreeRTOSConfig lock (C-01/C-02/H-08/H-09)")


def check_objects_and_tasks() -> None:
    header = RTOS_H.read_text(encoding="utf-8")
    source = RTOS_C.read_text(encoding="utf-8")
    hooks = HOOKS_C.read_text(encoding="utf-8")

    for obj in ("xI2CMutex", "xDataMutex", "xAfeAlertSem",
                "xCanTxQueue", "xCanRxQueue", "xCcSampleQueue",
                "xSysEvents"):
        require(obj in header and obj in source,
                f"IPC object {obj} missing")

    # Seven task entries with C-01 priorities.
    for task in ("Task_Protect", "Task_Sample", "Task_State", "Task_SOC",
                 "Task_Balance", "Task_CANTx", "Task_CANRx"):
        require(task in header and task in source, f"task {task} missing")

    require("APP_RTOS_PRIO_PROTECT                   (5)" in header,
            "Protect priority != 5")
    require("APP_RTOS_PRIO_SAMPLE                    (4)" in header,
            "Sample priority != 4")
    require("APP_RTOS_PRIO_STATE                     (3)" in header,
            "State priority != 3")
    require("APP_RTOS_PRIO_SOC                       (3)" in header,
            "SOC priority != 3")
    require("APP_RTOS_PRIO_BALANCE                   (2)" in header,
            "Balance priority != 2")
    require("APP_RTOS_PRIO_CAN_TX                    (2)" in header,
            "CANTx priority != 2")
    require("APP_RTOS_PRIO_CAN_RX                    (2)" in header,
            "CANRx priority != 2")

    # Hooks.
    require("vApplicationAssertFailedHandler" in hooks,
            "assert handler implementation missing")
    require("vApplicationMallocFailedHook" in hooks,
            "malloc hook implementation missing")
    require("vApplicationStackOverflowHook" in hooks,
            "stack overflow hook implementation missing")
    require("vApplicationIdleHook" in hooks,
            "idle hook implementation missing")
    print("PASS: RTOS objects + seven-task skeleton + hooks")


def check_main_integration() -> None:
    main = MAIN_C.read_text(encoding="utf-8")
    require("App_Rtos_CreateObjects" in main, "objects not created in main")
    require("App_Rtos_CreateTasks" in main, "tasks not created in main")
    require("vTaskStartScheduler" in main, "scheduler not started in main")
    # Order: objects before tasks before scheduler.
    require(main.index("App_Rtos_CreateObjects") <
            main.index("App_Rtos_CreateTasks"), "objects/tasks order wrong")
    require(main.index("App_Rtos_CreateTasks") <
            main.index("vTaskStartScheduler"), "tasks/scheduler order wrong")
    print("PASS: main integration order (objects->tasks->scheduler)")


def check_boundaries() -> None:
    header = RTOS_H.read_text(encoding="utf-8")
    source = RTOS_C.read_text(encoding="utf-8")

    # Strip /* */ and // comments so future-phase responsibility notes in
    # comments do not trip the executable-code boundary check.
    code = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)

    for token in ("g_bms_data", "EXTI", "CAN_Init",
                  "BQ76940_WriteBlock", "BQ76940_ReadBlock",
                  "FLASH_", "IWDG_", "PROTECT1", "SYS_CTRL2", "CELLBAL",
                  "SYS_STAT", "CC_READY", "xEventGroupSetBits"):
        require(token not in code,
                f"forbidden Phase 7+/hardware symbol in app_rtos.c: {token}")
    # Task bodies must be placeholders in Phase 6 (no functional logic).
    for task_body in ("Task_Protect", "Task_Sample"):
        idx = source.find(f"void {task_body}(")
        require(idx >= 0, f"{task_body} missing")
        body = source[idx:source.find("}", idx)]
        require("Phase 7:" in body or "Phase 8:" in body or
                "Phase 9:" in body or "Phase 10:" in body or
                "Phase 11:" in body,
                f"{task_body} body is not a documented Phase 6 skeleton")
    print("PASS: Phase 6 boundary (no hardware/ALERT/CAN/Flash in RTOS layer)")


def check_execution() -> None:
    simulator = SIM_LOG.read_text(encoding="utf-8", errors="replace")
    require("PHASE6_TEST_COMPLETED=1" in simulator, "C harness did not complete")
    require("PHASE6_TEST_FAILURES=0" in simulator, "C harness reported failures")
    require("*** error" not in simulator.lower(), "simulator command error")

    test_map = TEST_MAP.read_text(encoding="utf-8", errors="replace")
    for token in (
        "ARM Compiler 5.06 update 7 (build 960)",
        "tasks.o", "queue.o", "heap_4.o", "port.o",
        "app_rtos.o", "test_phase6_objects.o", "test_phase6_tasks.o",
    ):
        require(token in test_map, f"actual-C test map missing {token}")

    require(TEST_AXF.stat().st_mtime >= max(p.stat().st_mtime for p in TEST_SOURCES),
            "Phase 6 test AXF is stale relative to its sources")
    require(SIM_LOG.stat().st_mtime >= TEST_AXF.stat().st_mtime,
            "Phase 6 simulator log is stale relative to test AXF")
    print("PASS: ARMCC5 Simulator actual-C execution (completed=1 failures=0)")


def main() -> int:
    checks = (
        ("Phase 1..5 input hashes + Phase 6 target", check_regression),
        ("FreeRTOSConfig lock", check_freertos_config),
        ("RTOS objects + seven tasks + hooks", check_objects_and_tasks),
        ("main integration order", check_main_integration),
        ("Phase 6 driver boundary", check_boundaries),
        ("ARMCC5 Simulator execution", check_execution),
    )
    try:
        for name, function in checks:
            function()
    except (AssertionError, FileNotFoundError, ET.ParseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("PHASE6_STATIC_AND_EXECUTION_CHECKS: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
