#!/usr/bin/env python3
"""Independent/static Phase 2 gate checks; never invokes GCC."""

from __future__ import annotations

import hashlib
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "APP"
PROJECT = FW / "Project" / "Keil" / "BMS_V1.uvprojx"
BUILD_LOG = FW / "Project" / "Keil" / "Build" / "BMS_V1_Phase2_build.log"
SIM_LOG = FW / "Tests" / "Build" / "Phase2" / "phase2_simulator.log"

EXPECTED_APP_HASHES = {
    "bms_data.c": "377afb18c92d9fc28e0957256348d676944e880c0bf15cd6386b1812950cc59a",
    "bms_data.h": "1e5e240514aaeb5ceee93f070f8d4febeea0e2d60a134ec8df099d4b5e663eac",
    "bms_fault.c": "de387a67aa496aaea2d3732ad81ab85933dba7016999df6789505b5487b7d28e",
    "bms_fault.h": "61df623e8173abbea7483763b24bbac95ab4cfb3738900d256395e233bca13f4",
    "bms_state.h": "be487ed78e62a03d018222095f0acdf5dddf79a2b0e42e81da35a8a48c845330",
    "bms_types.h": "69fbe5c6ecc0bd57284b326c85c793a5622c400273ff36520d5a05d4bab01306",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def crc8(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_project() -> None:
    tree = ET.parse(PROJECT)
    target = tree.find("./Targets/Target")
    require(target is not None, "missing BMS_V1 target")
    require(target.findtext("TargetName") == "BMS_V1", "wrong target name")
    require(target.findtext("ToolsetName") == "ARM-ADS", "wrong toolset")
    require(target.findtext("pCCUsed") ==
            "5060960::V5.06 update 7 (build 960)::ARMCC",
            "ARMCC5 lock drifted")
    require(target.findtext("uAC6") == "0", "ArmClang enabled")
    common = target.find("./TargetOption/TargetCommonOption")
    require(common is not None, "missing target common options")
    require(common.findtext("Device") == "STM32F103C8", "wrong MCU")
    cpu = common.findtext("Cpu", "")
    require("IROM(0x08000000,0x0000F400)" in cpu, "wrong IROM")
    require("IRAM(0x20000000,0x00005000)" in cpu, "wrong IRAM")
    defines = target.findtext(
        "./TargetOption/TargetArmAds/Cads/VariousControls/Define", ""
    )
    require(defines == "STM32F10X_MD,USE_STDPERIPH_DRIVER", "define drift")
    paths = [node.text or "" for node in target.findall(".//FilePath")]
    joined = "\n".join(paths).lower()
    for expected in (
        "bsp_clock.c", "bsp_gpio.c", "bsp_timer.c", "soft_i2c.c",
        "crc8_bq76940.c", "stm32f10x_rcc.c", "stm32f10x_gpio.c",
        "stm32f10x_tim.c", "startup_stm32f10x_md.s",
    ):
        require(expected in joined, f"missing target source {expected}")
    require("stm32f10x_i2c.c" not in joined, "hardware-I2C SPL source linked")
    startup_paths = [p for p in paths if "startup_stm32f10x_" in p.lower()]
    require(len(startup_paths) == 1 and startup_paths[0].lower().endswith("_md.s"),
            "startup is not uniquely MD")


def check_models_unchanged() -> None:
    for name, expected in EXPECTED_APP_HASHES.items():
        require(sha256(FW / "App" / name) == expected,
                f"Phase 1 public model changed: {name}")


def check_sources() -> None:
    clock = (FW / "Driver" / "bsp_clock.c").read_text(encoding="utf-8")
    for token in (
        "RCC_CR_HSEON", "RCC_CR_HSERDY", "RCC_CR_PLLON", "RCC_CR_PLLRDY",
        "RCC_CFGR_SW_PLL", "RCC_CFGR_SWS_PLL", "RCC_CFGR_PLLMULL9",
        "RCC_CFGR_PPRE1_DIV2", "RCC_GetClocksFreq",
    ):
        require(token in clock, f"clock verification missing {token}")

    timer = (FW / "Driver" / "bsp_timer.c").read_text(encoding="utf-8")
    config = (FW / "Config" / "bms_config.h").read_text(encoding="utf-8")
    require("TIM3" in timer and "TIM_GenerateEvent" in timer, "TIM3 not initialized")
    require("BMS_TIM3_PRESCALER                       (71U)" in config,
            "TIM3 PSC drift")
    require("BMS_TIM3_AUTORELOAD                      (0xFFFFU)" in config,
            "TIM3 ARR drift")
    require("BSP_TIME_DELTA_US16" in timer, "wrap helper not used")

    gpio = (FW / "Driver" / "bsp_gpio.c").read_text(encoding="utf-8")
    require("GPIO_Mode_Out_OD" in gpio, "PB8/PB9 not open drain")
    require("GPIO_ReadInputDataBit" in gpio, "physical IDR is not read")

    soft = (FW / "Driver" / "soft_i2c.c").read_text(encoding="utf-8")
    header = (FW / "Driver" / "soft_i2c.h").read_text(encoding="utf-8")
    for token in (
        "BSP_SoftI2C_ReadByteBegin", "BSP_SoftI2C_SendReadResponse",
        "SOFT_I2C_RECOVERY_PULSES", "BSP_SoftI2C_WaitBusIdle",
        "generic I2C bus-clear strategy only", "cleanup:",
    ):
        require(token in soft or token in header, f"soft-I2C boundary missing {token}")
    require("SOFT_I2C_RECOVERY_PULSES    (9U)" in soft, "recovery pulse count drift")

    main = (FW / "User" / "main.c").read_text(encoding="utf-8")
    require("BSP_Clock_Verify" in main, "clock verification not called")
    require(main.index("BSP_Clock_Verify") < main.index("BSP_GPIO_Init"),
            "hardware initialized before clock verification")
    forbidden_main = ("BQ76940_", "Protect", "SampleTask", "CAN_", "vTaskStartScheduler")
    require(not any(token in main for token in forbidden_main),
            "Phase 3+ behavior entered main")

    production = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in FW.rglob("*")
        if path.suffix.lower() in {".c", ".h"} and "Tests" not in path.parts
    )
    for pattern in (r"\bHAL_", r"\bosThread", r"\bI2C_Init\s*\(",
                    r"\bI2C_GenerateSTART\s*\(", r"\bxTaskCreate\s*\("):
        require(re.search(pattern, production) is None,
                f"forbidden production symbol matched {pattern}")
    for forbidden_file in (
        "bq76940.c", "bms_protect.c", "bms_balance.c", "bms_soc.c", "bsp_can.c"
    ):
        require(not (FW / "Driver" / forbidden_file).exists() and
                not (FW / "App" / forbidden_file).exists(),
                f"premature file exists: {forbidden_file}")


def check_crc_oracle() -> None:
    vectors = {
        b"123456789": 0xF4,
        bytes.fromhex("10 0B 19"): 0x7A,
        bytes.fromhex("10 04 18"): 0xBE,
        bytes.fromhex("10 04 10"): 0x86,
        bytes.fromhex("40"): 0xC7,
        bytes.fromhex("11 12"): 0x3C,
        bytes.fromhex("34"): 0x8C,
        bytes.fromhex("56"): 0xA5,
        bytes.fromhex("00 FF AA 55"): 0x1D,
    }
    for data, expected in vectors.items():
        require(crc8(data) == expected, f"independent CRC vector failed: {data.hex()}")


def check_execution_and_build() -> None:
    simulator = SIM_LOG.read_text(encoding="utf-8", errors="replace")
    require("PHASE2_TEST_COMPLETED=1" in simulator, "C harness did not complete")
    require("PHASE2_TEST_FAILURES=0" in simulator, "C harness reported failures")
    require("*** error" not in simulator.lower(), "simulator command error")

    build = BUILD_LOG.read_text(encoding="utf-8", errors="replace")
    require("V5.06 update 7 (build 960)" in build, "wrong compiler in build log")
    require("Rebuild target 'BMS_V1'" in build, "target evidence is not a Rebuild")
    require("0 Error(s), 0 Warning(s)" in build, "ARMCC5 target build failed")
    require("Program Size:" in build, "missing target sizes")


def main() -> int:
    checks = (
        ("project/toolchain/boundary", check_project),
        ("Phase1 public model hashes", check_models_unchanged),
        ("production source boundaries", check_sources),
        ("independent CRC oracle", check_crc_oracle),
        ("ARMCC5 simulator + target build", check_execution_and_build),
    )
    try:
        for name, function in checks:
            function()
            print(f"PASS: {name}")
    except (AssertionError, FileNotFoundError, ET.ParseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("PHASE2_STATIC_AND_EXECUTION_CHECKS: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
