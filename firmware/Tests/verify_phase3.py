#!/usr/bin/env python3
"""Independent/static Phase 3 gate checks; never invokes GCC."""

from __future__ import annotations

import hashlib
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware"
PROJECT = FW / "Project" / "Keil" / "BMS_V1.uvprojx"
BUILD_LOG = FW / "Project" / "Keil" / "Build" / "BMS_V1_Phase3_build.log"
BUILD_AXF = FW / "Project" / "Keil" / "Objects" / "BMS_V1.axf"
SIM_LOG = FW / "Tests" / "Build" / "Phase3" / "phase3_simulator.log"
TEST_MAP = FW / "Tests" / "Build" / "Phase3" / "phase3_tests.map"
TEST_AXF = FW / "Tests" / "Build" / "Phase3" / "phase3_tests.axf"
PHASE2_REPORT = ROOT / "deliverables" / "phase2" / "BMS_V1_Phase2_Report.md"

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

PHASE2_REPORT_HASH = "4adbed1e30a6b60f8e0fddad1094d61f5de0bc0a48bf075ac40a585780581a34"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def crc8(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def macro_value(text: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\((0x[0-9A-Fa-f]+|[0-9]+)U\)\s*$",
        text,
        re.MULTILINE,
    )
    require(match is not None, f"missing or nonliteral macro {name}")
    return int(match.group(1), 0)


def check_phase2_input() -> None:
    require(sha256(PHASE2_REPORT) == PHASE2_REPORT_HASH,
            "validated Phase 2 report revision drifted")
    for relative, expected in EXPECTED_PHASE2_HASHES.items():
        require(sha256(FW / relative) == expected,
                f"validated Phase 2 input drifted: {relative}")
    for name, expected in EXPECTED_APP_HASHES.items():
        require(sha256(FW / "App" / name) == expected,
                f"Phase 1 public model changed: {name}")


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
        "bq76940.c", "crc8_bq76940.c", "soft_i2c.c", "bsp_clock.c",
        "bsp_gpio.c", "bsp_timer.c", "stm32f10x_rcc.c",
        "stm32f10x_gpio.c", "stm32f10x_tim.c", "startup_stm32f10x_md.s",
    ):
        require(expected in joined, f"missing target source {expected}")
    require("stm32f10x_i2c.c" not in joined, "hardware-I2C SPL source linked")
    startup_paths = [p for p in paths if "startup_stm32f10x_" in p.lower()]
    require(len(startup_paths) == 1 and startup_paths[0].lower().endswith("_md.s"),
            "startup is not uniquely MD")


def check_registers() -> None:
    text = (FW / "Driver" / "bq76940_regs.h").read_text(encoding="utf-8")
    expected = {
        "BQ76940_I2C_ADDRESS_7BIT": 0x08,
        "BQ76940_I2C_WIRE_WRITE": 0x10,
        "BQ76940_I2C_WIRE_READ": 0x11,
        "BQ76940_REG_SYS_STAT": 0x00,
        "BQ76940_REG_CELLBAL1": 0x01,
        "BQ76940_REG_CELLBAL2": 0x02,
        "BQ76940_REG_CELLBAL3": 0x03,
        "BQ76940_REG_SYS_CTRL1": 0x04,
        "BQ76940_REG_SYS_CTRL2": 0x05,
        "BQ76940_REG_PROTECT1": 0x06,
        "BQ76940_REG_PROTECT2": 0x07,
        "BQ76940_REG_PROTECT3": 0x08,
        "BQ76940_REG_OV_TRIP": 0x09,
        "BQ76940_REG_UV_TRIP": 0x0A,
        "BQ76940_REG_CC_CFG": 0x0B,
        "BQ76940_CC_CFG_REQUIRED_VALUE": 0x19,
        "BQ76940_REG_BAT_HI": 0x2A,
        "BQ76940_REG_BAT_LO": 0x2B,
        "BQ76940_REG_TS1_HI": 0x2C,
        "BQ76940_REG_TS1_LO": 0x2D,
        "BQ76940_REG_TS2_HI": 0x2E,
        "BQ76940_REG_TS2_LO": 0x2F,
        "BQ76940_REG_TS3_HI": 0x30,
        "BQ76940_REG_TS3_LO": 0x31,
        "BQ76940_REG_CC_HI": 0x32,
        "BQ76940_REG_CC_LO": 0x33,
        "BQ76940_REG_ADCGAIN1": 0x50,
        "BQ76940_REG_ADCOFFSET": 0x51,
        "BQ76940_REG_ADCGAIN2": 0x59,
        "BQ76940_ADCGAIN1_MASK": 0x0C,
        "BQ76940_ADCGAIN2_MASK": 0xE0,
        "BQ76940_ADC_GAIN_BASE_UV_PER_LSB": 365,
        "BQ76940_ADC_GAIN_MAX_UV_PER_LSB": 396,
        "BQ76940_CELL_RAW14_MASK": 0x3FFF,
    }
    for cell in range(1, 16):
        expected[f"BQ76940_REG_VC{cell}_HI"] = 0x0C + ((cell - 1) * 2)
        expected[f"BQ76940_REG_VC{cell}_LO"] = 0x0D + ((cell - 1) * 2)
    for name, value in expected.items():
        require(macro_value(text, name) == value,
                f"official register/constant mismatch: {name}")
    require("DEVICE_ID" not in text, "invented device identity register")


def check_transport_and_boundaries() -> None:
    source = (FW / "Driver" / "bq76940.c").read_text(encoding="utf-8")
    header = (FW / "Driver" / "bq76940.h").read_text(encoding="utf-8")
    main = (FW / "User" / "main.c").read_text(encoding="utf-8")
    tests = (FW / "Tests" / "test_phase3_transport.c").read_text(encoding="utf-8")

    for token in (
        "BQ76940_CRC8_FirstWrite", "BQ76940_CRC8_NextByte",
        "BQ76940_CRC8_FirstRead", "SoftI2C_RepeatedStart",
        "SoftI2C_ReadByteBegin", "SoftI2C_SendReadResponse",
        "SOFT_I2C_MASTER_ACK", "SOFT_I2C_MASTER_NACK",
        "uint8_t staged[BQ76940_MAX_BLOCK_LENGTH]",
        "memcpy(data, staged, length)", "BQ76940_STATUS_CRC_MISMATCH",
        "BQ76940_STATUS_CRC_REJECTED", "BQ76940_STATUS_CALIBRATION_INVALID",
        "BQ76940_STATUS_RANGE_ERROR",
    ):
        require(token in source or token in header, f"transport contract missing {token}")
    require(re.search(
        r"BQ76940_CRC8_FirstRead\s*\(\s*BQ76940_I2C_WIRE_READ\s*,\s*staged\[index\]\s*\)",
        source,
    ) is not None, "first read CRC framing drifted")
    require(re.search(
        r"BQ76940_CRC8_FirstWrite\s*\(\s*BQ76940_I2C_WIRE_WRITE\s*,\s*start_register\s*,\s*data\[index\]\s*\)",
        source,
    ) is not None, "first write CRC framing drifted")
    require(source.index("SoftI2C_Stop(device->bus)") <
            source.index("memcpy(data, staged, length)"),
            "read output commits before STOP succeeds")
    adjacent = source[source.index("BQ76940_ReadAdjacentU16"):source.index(
        "BQ76940_DecodeCalibration")]
    require(adjacent.count("BQ76940_ReadBlock") == 1 and
            "BQ76940_ReadByte" not in adjacent,
            "adjacent pair is not one block transaction")
    require(not re.search(r"\b(RMW|ReadModify|ModifyBits)\b", source + header),
            "unchecked public RMW interface exists")
    require("BQ76940_CC_CFG_REQUIRED_VALUE" not in source + main,
            "CC_CFG is written by default")
    require("BQ76940_Init" in main, "BQ transport context is not initialized")
    for forbidden_call in ("BQ76940_Read", "BQ76940_Write", "BQ76940_Probe"):
        require(forbidden_call not in main, f"default main performs {forbidden_call}")

    for token in (
        "write_one_trace", "write_three_trace", "read_one_trace",
        "read_two_trace", "read_three_trace", "read_bad_crc",
        "SOFT_I2C_STATUS_NACK_ADDRESS", "SOFT_I2C_STATUS_NACK_DATA",
        "SOFT_I2C_STATUS_TIMEOUT", "SOFT_I2C_STATUS_STATE_ERROR",
        "BQ76940_MAX_BLOCK_LENGTH + 1U", "0x4000U",
    ):
        require(token in tests or token in (FW / "Tests" / "test_phase3_decode.c").read_text(encoding="utf-8"),
                f"required executed test case missing {token}")

    production = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in FW.rglob("*")
        if path.suffix.lower() in {".c", ".h"} and "Tests" not in path.parts
    )
    for pattern in (
        r"\bHAL_", r"\bosThread", r"\bI2C_Init\s*\(",
        r"\bI2C_GenerateSTART\s*\(", r"\bxTaskCreate\s*\(",
        r"\bEXTI1_IRQHandler\s*\(", r"\bProtectTask\s*\(",
        r"\bSampleTask\s*\(", r"\bBQ76940_ReadAll13Cells\s*\(",
        r"\bBQ76940_ReadCell1Mv\s*\(",
    ):
        require(re.search(pattern, production) is None,
                f"forbidden Phase 4+/framework symbol matched {pattern}")
    for forbidden_name in (
        "bms_protect.c", "bms_balance.c", "bms_soc.c", "bsp_can.c",
        "app_tasks.c", "bq76940_measurement.c",
    ):
        require(not any(path.name.lower() == forbidden_name for path in FW.rglob("*")),
                f"premature Phase 4+ file exists: {forbidden_name}")


def check_numeric_oracles() -> None:
    crc_vectors = {
        b"123456789": 0xF4,
        bytes.fromhex("10 0B 19"): 0x7A,
        bytes.fromhex("10 04 18"): 0xBE,
        bytes.fromhex("10 04 10"): 0x86,
        bytes.fromhex("40"): 0xC7,
        bytes.fromhex("00"): 0x00,
        bytes.fromhex("11 12"): 0x3C,
        bytes.fromhex("34"): 0x8C,
        bytes.fromhex("56"): 0xA5,
        bytes.fromhex("11 00"): 0x42,
        bytes.fromhex("11 FF"): 0xB1,
        bytes.fromhex("FF"): 0xF3,
        bytes.fromhex("00 FF AA 55"): 0x1D,
    }
    for data, expected in crc_vectors.items():
        require(crc8(data) == expected, f"independent CRC vector failed: {data.hex()}")

    gain_vectors = (
        (0x00, 0x00, 365), (0x0C, 0xE0, 396),
        (0x04, 0xA0, 378), (0x08, 0x60, 384), (0xF7, 0xBF, 378),
    )
    for gain1, gain2, expected in gain_vectors:
        trim = ((gain1 & 0x0C) << 1) | ((gain2 & 0xE0) >> 5)
        require(365 + trim == expected, "independent ADC gain vector failed")

    offsets = {0x00: 0, 0x01: 1, 0x7F: 127, 0x80: -128, 0x81: -127, 0xFF: -1}
    for raw, expected in offsets.items():
        decoded = raw - 256 if raw & 0x80 else raw
        require(decoded == expected, "independent offset vector failed")

    conversions = (
        (0x1800, 380, 30, 2365), (0x1F10, 380, 30, 3052),
        (0x1000, 365, -128, 1367), (0x3FFF, 396, 127, 6615),
        (0x0019, 380, 0, 10),
    )
    for raw, gain, offset, expected_mv in conversions:
        microvolts = raw * gain + offset * 1000
        require(microvolts >= 0 and (microvolts + 500) // 1000 == expected_mv,
                "independent cell conversion vector failed")
    require((((0xFF & 0x3F) << 8) | 0xFF) == 0x3FFF,
            "independent raw14 mask vector failed")


def check_execution_and_build() -> None:
    simulator = SIM_LOG.read_text(encoding="utf-8", errors="replace")
    require("PHASE3_TEST_COMPLETED=1" in simulator, "C harness did not complete")
    require("PHASE3_TEST_FAILURES=0" in simulator, "C harness reported failures")
    require("*** error" not in simulator.lower(), "simulator command error")

    test_map = TEST_MAP.read_text(encoding="utf-8", errors="replace")
    for token in (
        "ARM Compiler 5.06 update 7 (build 960)", "bq76940.o",
        "crc8_bq76940.o", "test_phase3_transport.o", "test_phase3_decode.o",
        "SoftI2C_Start", "BQ76940_ReadBlock",
    ):
        require(token in test_map, f"actual-C test map missing {token}")
    test_sources = (
        FW / "Driver" / "bq76940.c",
        FW / "Driver" / "crc8_bq76940.c",
        FW / "Tests" / "test_phase3_main.c",
        FW / "Tests" / "test_phase3_transport.c",
        FW / "Tests" / "test_phase3_decode.c",
    )
    require(TEST_AXF.stat().st_mtime >= max(path.stat().st_mtime for path in test_sources),
            "Phase 3 test AXF is stale relative to its sources")
    require(SIM_LOG.stat().st_mtime >= TEST_AXF.stat().st_mtime,
            "Phase 3 simulator log is stale relative to test AXF")

    build = BUILD_LOG.read_text(encoding="utf-8", errors="replace")
    require("V5.06 update 7 (build 960)" in build, "wrong compiler in build log")
    require("Rebuild target 'BMS_V1'" in build, "target evidence is not a Rebuild")
    require("0 Error(s), 0 Warning(s)" in build, "ARMCC5 target build failed")
    match = re.search(
        r"Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)",
        build,
    )
    require(match is not None, "missing target sizes")
    sizes = tuple(int(value) for value in match.groups())
    require(sizes[0] >= 3084 and sizes[1] >= 268 and sizes[3] >= 1896,
            "Phase 3 image unexpectedly smaller than Phase 2 baseline")
    production_sources = (
        FW / "Driver" / "bq76940.c",
        FW / "Driver" / "bq76940.h",
        FW / "Driver" / "bq76940_regs.h",
        FW / "User" / "main.c",
        PROJECT,
    )
    require(BUILD_AXF.stat().st_mtime >= max(path.stat().st_mtime for path in production_sources),
            "production AXF is stale relative to Phase 3 sources/project")
    print("PHASE3_PROGRAM_SIZE: Code={} RO={} RW={} ZI={}".format(*sizes))


def main() -> int:
    checks = (
        ("validated Phase2 input hashes", check_phase2_input),
        ("project/toolchain/boundary", check_project),
        ("official BQ register constants", check_registers),
        ("transport/atomicity/Phase4 boundary", check_transport_and_boundaries),
        ("independent CRC/calibration/conversion oracles", check_numeric_oracles),
        ("ARMCC5 actual-C simulator + target Rebuild", check_execution_and_build),
    )
    try:
        for name, function in checks:
            function()
            print(f"PASS: {name}")
    except (AssertionError, FileNotFoundError, ET.ParseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("PHASE3_STATIC_AND_EXECUTION_CHECKS: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
