#!/usr/bin/env python3
"""Static Phase 1 boundary checks; this is not an ARMCC5 build."""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "APP"
PROJECT = FIRMWARE / "Project" / "Keil" / "BMS_V1.uvprojx"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def project_file_paths(root: ET.Element) -> list[str]:
    return [node.text or "" for node in root.findall(".//FilePath")]


def resolve_keil_path(path_text: str) -> Path:
    path_text = path_text.replace("\\", "/")
    return (PROJECT.parent / path_text).resolve()


def verify_tree() -> None:
    required_directories = {
        "App",
        "Driver",
        "Protocol",
        "Service",
        "Config",
        "User",
        "Project",
        "Tests",
    }
    actual = {path.name for path in FIRMWARE.iterdir() if path.is_dir()}
    require(required_directories <= actual, "required firmware directories missing")

    required_files = [
        "App/bms_types.h",
        "App/bms_state.h",
        "App/bms_fault.h",
        "App/bms_fault.c",
        "App/bms_data.h",
        "App/bms_data.c",
        "Config/bms_build_assert.h",
        "Config/bms_config.h",
        "Config/bms_memory_map.h",
        "User/main.c",
        "Project/Keil/BMS_V1.uvprojx",
        "Tests/test_phase1_models.c",
        "Project/SRAM_Budget.md",
    ]
    for relative in required_files:
        require((FIRMWARE / relative).is_file(), f"missing {relative}")

    forbidden_files = {
        "bq76940.c",
        "soft_i2c.c",
        "bsp_can.c",
        "bms_soc.c",
        "bms_balance.c",
        "bms_protect.c",
        "app_tasks.c",
        "FreeRTOSConfig.h",
    }
    found = {path.name for path in FIRMWARE.rglob("*") if path.is_file()}
    require(not (forbidden_files & found), "Phase 2+ source or old RTOS config found")


def verify_project() -> None:
    xml_root = ET.parse(PROJECT).getroot()
    target = xml_root.find("./Targets/Target")
    require(target is not None, "Keil target missing")
    require(target.findtext("TargetName") == "BMS_V1", "wrong target name")
    require(target.findtext("ToolsetName") == "ARM-ADS", "not ARMCC5 project")
    require(target.findtext("pCCUsed") ==
            "5060960::V5.06 update 7 (build 960)::ARMCC",
            "wrong ARMCC5 compiler lock")
    require(target.findtext("uAC6") == "0", "ArmClang must be disabled")

    common = target.find("./TargetOption/TargetCommonOption")
    require(common is not None, "common target options missing")
    require(common.findtext("Device") == "STM32F103C8", "wrong device")
    cpu = common.findtext("Cpu") or ""
    require("IROM(0x08000000,0x0000F400)" in cpu, "CPU IROM not 61 KiB")
    require("IRAM(0x20000000,0x00005000)" in cpu, "CPU IRAM not 20 KiB")

    cads = target.find("./TargetOption/TargetArmAds/Cads")
    require(cads is not None, "ARMCC5 C settings missing")
    defines = {
        value.strip()
        for value in (cads.findtext("./VariousControls/Define") or "").split(",")
        if value.strip()
    }
    require(defines == {"STM32F10X_MD", "USE_STDPERIPH_DRIVER"},
            "compiler defines drifted")

    memories = target.find("./TargetOption/TargetArmAds/ArmAdsMisc/OnChipMemories")
    require(memories is not None, "target memory settings missing")
    require(memories.findtext("./IROM/StartAddress") == "0x8000000",
            "IROM base drifted")
    require(memories.findtext("./IROM/Size") == "0xF400", "IROM size drifted")
    require(memories.findtext("./IRAM/StartAddress") == "0x20000000",
            "IRAM base drifted")
    require(memories.findtext("./IRAM/Size") == "0x5000", "IRAM size drifted")

    files = project_file_paths(target)
    startup_files = [path for path in files if "startup_stm32f10x_" in path.lower()]
    require(len(startup_files) == 1, "target must contain exactly one startup")
    require(startup_files[0].lower().endswith("startup_stm32f10x_md.s"),
            "target does not use MD startup")
    require(not any("freertos" in path.lower() for path in files),
            "FreeRTOS must not be linked in Phase 1")
    require(not any("libarary\\stm32f10x_" in path.lower() and
                    path.lower().endswith(".c") for path in files),
            "unneeded SPL peripheral source linked")

    for path_text in files:
        require(resolve_keil_path(path_text).is_file(),
                f"Keil source path does not exist: {path_text}")


def verify_constants_and_boundaries() -> None:
    config = (FIRMWARE / "Config" / "bms_config.h").read_text(encoding="utf-8")
    memory = (FIRMWARE / "Config" / "bms_memory_map.h").read_text(encoding="utf-8")
    fault = (FIRMWARE / "App" / "bms_fault.h").read_text(encoding="utf-8")
    state = (FIRMWARE / "App" / "bms_state.h").read_text(encoding="utf-8")
    main = (FIRMWARE / "User" / "main.c").read_text(encoding="utf-8")

    required_literals = {
        "BMS_CELL_COUNT": "13U",
        "BMS_REFERENCE_CAPACITY_MAH": "20000U",
        "BMS_RSENSE_REFERENCE_UOHM": "4000U",
        "BMS_NTC_REFERENCE_OHM": "10000U",
    }
    for name, value in required_literals.items():
        require(re.search(rf"#define\s+{name}\s+\({value}\)", config) is not None,
                f"configuration constant {name} drifted")

    for literal in ("0x08000000UL", "0x0000F400UL", "0x0800F400UL",
                    "0x0800F800UL", "0x0800FC00UL", "0x08010000UL"):
        require(literal in memory, f"memory boundary missing: {literal}")

    fault_ids = re.findall(
        r"BMS_FAULT_ID_(?!COUNT\b)[A-Z0-9_]+\s*=\s*(\d+)", fault
    )
    require([int(value) for value in fault_ids] == list(range(20)),
            "fault identifiers are not unique contiguous values 0..19")

    state_ids = re.findall(r"BMS_STATE_(?:INIT|STANDBY|CHARGE|DISCHARGE|FAULT)\s*=\s*(\d+)",
                           state)
    require([int(value) for value in state_ids] == list(range(5)),
            "state identifiers are not stable values 0..4")
    require("SystemInit(" not in main, "main must not call SystemInit again")
    require("FML_Data_Init();" in main, "main must initialize shared data")


def verify_phase_boundary() -> None:
    text_files = [
        path for path in FIRMWARE.rglob("*")
        if path.is_file() and path.suffix.lower() in {".c", ".h", ".uvprojx"}
    ]
    content = "\n".join(path.read_text(encoding="utf-8") for path in text_files)
    forbidden_tokens = (
        "xTaskCreate",
        "vTaskStartScheduler",
        "HAL_Init",
        "osThreadNew",
        "BQ_WriteReg",
        "IWDG_ReloadCounter",
    )
    for token in forbidden_tokens:
        require(token not in content, f"out-of-scope symbol found: {token}")


def main() -> int:
    checks = (
        verify_tree,
        verify_project,
        verify_constants_and_boundaries,
        verify_phase_boundary,
    )
    for check in checks:
        check()
        print(f"PASS: {check.__name__}")
    print("PASS: Phase 1 static boundary verification")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, ET.ParseError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
