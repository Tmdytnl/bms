#!/usr/bin/env python3
"""Deterministic BSP/DRV/FML/APL dependency and ownership gate."""

from __future__ import annotations

import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FAILURES: list[str] = []


def production_files(layer: str) -> list[Path]:
    return sorted(
        path for path in (ROOT / "firmware" / layer).rglob("*")
        if path.suffix.lower() in {".c", ".h"}
    )


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    FAILURES.append(message)


def check(condition: bool, message: str) -> None:
    if condition:
        print(f"PASS: {message}")
    else:
        fail(message)


def reject_matches(paths: list[Path], patterns: dict[str, re.Pattern[str]],
                   description: str) -> None:
    violations: list[str] = []
    for path in paths:
        source = text(path)
        for label, pattern in patterns.items():
            if pattern.search(source):
                violations.append(f"{relative(path)} [{label}]")
    if violations:
        fail(f"{description}: " + ", ".join(violations))
    else:
        print(f"PASS: {description}")


fml = production_files("FML")
drv = production_files("DRV")
bsp = production_files("BSP")
apl = production_files("APL")
user = production_files("User")

rtos_patterns = {
    "RTOS include": re.compile(
        r'#\s*include\s*[<"](?:FreeRTOS|task|queue|semphr|event_groups)\.h[>"]'),
    "RTOS API/type": re.compile(
        r"\b(?:TickType_t|BaseType_t|TaskHandle_t|QueueHandle_t|"
        r"SemaphoreHandle_t|EventGroupHandle_t|xTask\w*|vTask\w*|"
        r"xQueue\w*|xSemaphore\w*|xEventGroup\w*|ulTaskNotifyTake|"
        r"taskENTER_CRITICAL|taskEXIT_CRITICAL|portYIELD_FROM_ISR)\b"),
}
reject_matches(fml, rtos_patterns, "FML contains no RTOS dependency")
reject_matches(drv, rtos_patterns, "DRV contains no RTOS dependency")

reject_matches(
    fml,
    {
        "BSP include": re.compile(r'#\s*include\s*"bsp_[^"]+"'),
        "MCU include": re.compile(r'#\s*include\s*[<"](?:stm32f10x|core_cm3|misc)\w*\.h[>"]'),
        "direct MCU symbol": re.compile(r"\b(?:GPIO|RCC|EXTI|CAN|USART|FLASH|NVIC|TIM)_[A-Za-z0-9_]+\s*\("),
    },
    "FML contains no BSP or direct STM32 peripheral dependency",
)

reject_matches(
    drv,
    {
        "upward include": re.compile(r'#\s*include\s*"(?:apl_|bms_)[^"]+"'),
    },
    "DRV contains no APL/FML business-header dependency",
)
reject_matches(
    bsp,
    {
        "upward include": re.compile(r'#\s*include\s*"(?:apl_|bms_)[^"]+"'),
    },
    "BSP contains no APL/FML business-header dependency",
)

main_path = ROOT / "firmware/User/main.c"
main = text(main_path) if main_path.is_file() else ""
check(
    main.count("#include") == 1 and
    '#include "apl_system.h"' in main and
    all(token in main for token in
        ("APL_SystemInit", "APL_SystemStart", "APL_SafeIdle")) and
    not re.search(r"\b(?:BMS_|BSP_|BQ76940_|SoftI2C_)\w*\s*\(", main),
    "main.c is a thin APL entry point",
)

expected_tasks = {
    "APL_TaskProtect": "apl_task_protect.c",
    "APL_TaskSample": "apl_task_sample.c",
    "APL_TaskState": "apl_task_state.c",
    "APL_TaskSoc": "apl_task_soc.c",
    "APL_TaskBalance": "apl_task_balance.c",
    "APL_TaskCanTx": "apl_task_can_tx.c",
    "APL_TaskCanRx": "apl_task_can_rx.c",
}
task_defs: dict[str, list[Path]] = {name: [] for name in expected_tasks}
all_firmware_c = sorted(
    path for layer in ("APL", "FML", "DRV", "BSP", "User")
    for path in (ROOT / "firmware" / layer).rglob("*.c")
)
for path in all_firmware_c:
    source = text(path)
    for name in expected_tasks:
        if re.search(rf"\bvoid\s+{name}\s*\(", source):
            task_defs[name].append(path)
check(
    all(len(task_defs[name]) == 1 and
        task_defs[name][0].parent.name == "Task" and
        task_defs[name][0].name == filename
        for name, filename in expected_tasks.items()),
    "exactly seven production task entries live in APL/Task",
)

rtos_c = text(ROOT / "firmware/APL/apl_rtos.c")
priority_values = (5, 4, 3, 3, 2, 2, 2)
rtos_h = text(ROOT / "firmware/APL/apl_rtos.h")
check(
    all(name in rtos_c for name in expected_tasks) and
    all(f"({value})" in rtos_h for value in set(priority_values)),
    "seven-task creation topology and frozen priorities remain in APL",
)

apl_public = text(ROOT / "firmware/APL/apl_rtos.h")
apl_internal_path = ROOT / "firmware/APL/apl_rtos_internal.h"
apl_internal = text(apl_internal_path) if apl_internal_path.is_file() else ""
raw_handles = (
    "xI2CMutex", "xDataMutex", "xAfeAlertSem", "xCanTxQueue",
    "xCanRxQueue", "xCcSampleQueue", "xSysEvents",
)
check(
    not re.search(r"^extern\s+.*Handle_t", apl_public, re.M) and
    all(handle in apl_internal for handle in raw_handles),
    "raw RTOS handles are confined to the APL-private registry",
)
internal_header_users = [
    relative(path) for path in (*fml, *drv, *bsp, *user)
    if 'apl_rtos_internal.h' in text(path)
]
check(
    not internal_header_users,
    "non-APL production code cannot include the private RTOS registry",
)

mutable_fml_externs = [
    relative(path) for path in fml
    if path.suffix.lower() == ".h" and
    re.search(r"^\s*extern\s+(?!const\b)", text(path), re.M)
]
check(
    not mutable_fml_externs,
    "FML public headers expose no mutable backing-store globals",
)
runtime_header_dependencies = {
    "bms_can.h": ("bms_data.h", "bms_fet_manager.h", "bms_protect.h",
                  "bms_recovery.h", "bms_state.h"),
    "bms_balance.h": ("bms_data.h", "bms_protect.h",
                      "bms_recovery.h", "bms_state.h"),
}
for header_name, forbidden_includes in runtime_header_dependencies.items():
    header_path = next(path for path in fml if path.name == header_name)
    header_source = text(header_path)
    check(
        all(f'#include "{include}"' not in header_source
            for include in forbidden_includes),
        f"{header_name} keeps pure calculation owner dependencies out of its runtime interface",
    )
production_text = "\n".join(text(path) for path in (*apl, *fml, *drv,
                                                      *bsp, *user))
check(
    "APL_SystemAfeDevice" not in production_text,
    "APL task dependencies are injected without a composition backchannel",
)

irq_definitions: dict[str, list[str]] = {
    "EXTI1_IRQHandler": [],
    "USB_LP_CAN1_RX0_IRQHandler": [],
}
for path in all_firmware_c:
    source = text(path)
    for handler in irq_definitions:
        if re.search(rf"\bvoid\s+{handler}\s*\(", source):
            irq_definitions[handler].append(relative(path))
check(
    all(files == ["firmware/APL/apl_irq.c"]
        for files in irq_definitions.values()),
    "RTOS-aware ALERT and CAN IRQ handoff is owned by APL",
)

def files_with_write(register: str) -> set[str]:
    owners: set[str] = set()
    pattern = re.compile(
        rf"BQ76940_WriteByte\s*\([^;]*\b{register}\b", re.S)
    for path in all_firmware_c:
        if pattern.search(text(path)):
            owners.add(relative(path))
    return owners


check(
    files_with_write("BQ76940_REG_SYS_CTRL2") == {
        "firmware/FML/Afe/bms_afe_startup.c",
        "firmware/FML/Fet/bms_fet_manager.c",
    },
    "SYS_CTRL2 writers remain startup plus FET sole runtime owner",
)
cellbal_writers = (
    files_with_write("BQ76940_REG_CELLBAL1") |
    files_with_write("BQ76940_REG_CELLBAL2") |
    files_with_write("BQ76940_REG_CELLBAL3")
)
check(
    cellbal_writers == {"firmware/FML/Balance/bms_balance.c"} and
    all(register in text(ROOT / "firmware/FML/Afe/bms_afe_startup.c")
        for register in ("BQ76940_REG_CELLBAL1", "BQ76940_REG_CELLBAL2",
                         "BQ76940_REG_CELLBAL3")),
    "CELLBAL direct writes remain in Balance; startup retains all-off staging",
)
check(
    files_with_write("BQ76940_REG_SYS_STAT") == {
        "firmware/FML/Afe/bms_afe_startup.c",
        "firmware/FML/Protect/bms_protect.c",
    },
    "SYS_STAT writers remain startup plus Protect sole runtime owner",
)

project = ROOT / "firmware/Project/Keil/BMS_V1.uvprojx"
project_paths: set[str] = set()
try:
    project_paths = {
        (node.text or "").strip()
        for node in ET.parse(project).findall(".//FilePath")
    }
except (ET.ParseError, OSError) as exc:
    fail(f"Keil project is not readable XML: {exc}")

tracked_legacy: list[str] = []
try:
    git_result = subprocess.run(
        ["git", "ls-files", "--", "firmware/App", "firmware/Driver"],
        cwd=ROOT, check=False, capture_output=True, text=True,
    )
    if git_result.returncode == 0:
        tracked_legacy = [
            line for line in git_result.stdout.splitlines()
            if Path(line).suffix.lower() in {".c", ".h"}
        ]
except OSError:
    # Source archives may omit .git; Keil membership remains the production truth.
    pass
project_legacy = [
    path for path in project_paths
    if re.match(r"^\.\.\\\.\.\\(?:App|Driver)\\.*\.[ch]$",
                path, re.I)
]
check(
    not tracked_legacy and not project_legacy,
    "legacy mixed App/Driver production sources are retired",
)

required_project_paths = {
    "..\\..\\APL\\apl_system.c",
    "..\\..\\APL\\apl_rtos.c",
    "..\\..\\APL\\apl_irq.c",
    *{
        f"..\\..\\APL\\Task\\{filename}"
        for filename in expected_tasks.values()
    },
}
check(required_project_paths <= project_paths,
      "Keil production project builds the APL composition and all seven tasks")

if FAILURES:
    print(f"ARCHITECTURE GATE FAIL ({len(FAILURES)} failure(s))")
    sys.exit(1)

print("ARCHITECTURE GATE PASS")
