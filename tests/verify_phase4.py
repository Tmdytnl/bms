#!/usr/bin/env python3
"""Independent/static Phase 4 gate checks; never invokes GCC.

Checks, independently of the C code under test:
  1. Phase 3 validated baseline input hashes still hold (regression).
  2. 13S logical->VC mapping oracle (VC9/VC14 skipped, 13 entries).
  3. Cell conversion oracle vs. the golden vectors embedded in the
     ARMCC5 test harness.
  4. BAT pack-voltage oracle (TI eq. 9) vs. golden vectors.
  5. CC raw decode + CC->mA oracle (TI eq. 3, truncating division).
  6. TS1 raw->resistance oracle (TI eq. 4/5) vs. golden vectors.
  7. Phase 4 boundary: no RTOS, no protection, no CAN, no HAL,
     no hardware I2C, no App/task headers in the Driver.
  8. ARMCC5 Simulator log + test AXF freshness.
"""

from __future__ import annotations

import hashlib
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "APP"
PROJECT = FW / "Project" / "Keil" / "BMS_V1.uvprojx"
MEASUREMENT_H = FW / "Driver" / "bq76940_measurement.h"
MEASUREMENT_C = FW / "Driver" / "bq76940_measurement.c"
SIM_LOG = FW / "Tests" / "Build" / "Phase4" / "phase4_simulator.log"
TEST_AXF = FW / "Tests" / "Build" / "Phase4" / "phase4_tests.axf"
TEST_MAP = FW / "Tests" / "Build" / "Phase4" / "phase4_tests.map"
TEST_SOURCES = (
    FW / "Driver" / "bq76940.c",
    FW / "Driver" / "bq76940_measurement.c",
    FW / "Driver" / "crc8_bq76940.c",
    FW / "Tests" / "test_phase4_main.c",
    FW / "Tests" / "test_phase4_mapping.c",
    FW / "Tests" / "test_phase4_measurement.c",
)

PHASE3_REPORT_HASH = "e43d30237901980a42604ba4b4d580ffa6efab9f0352a420bb52151c0cf6c018"
PHASE3_UVPROJX_HASH = "7155ec5f13c67659613c5b850b3b5f1b4c8ee08068fb06eb5091edb029500294"

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
    "User/main.c": "529e78df9b45a0910eca753dec43df06787cfea8b5a2bf888947ce877120e0e8",
}


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


def check_regression() -> None:
    require(sha256(ROOT / "deliverables" / "phase3" / "BMS_V1_Phase3_Report.md") ==
            PHASE3_REPORT_HASH,
            "Phase 3 report revision drifted")
    # uvprojx 在此阶段只授权增加 bq76940_measurement.c；验证增量 source list，
    # 保留全部既有输入且不允许无关 SPL 混入。
    require(sha256(PROJECT) != PHASE3_UVPROJX_HASH,
            "uvprojx was not extended for Phase 4")
    tree = ET.parse(PROJECT)
    paths = [node.text or "" for node in tree.findall(".//FilePath")]
    joined = "\n".join(paths).lower()
    for expected in (
        "bq76940.c", "crc8_bq76940.c", "soft_i2c.c", "bsp_clock.c",
        "bsp_gpio.c", "bsp_timer.c", "stm32f10x_rcc.c",
        "stm32f10x_gpio.c", "stm32f10x_tim.c", "startup_stm32f10x_md.s",
        "bq76940_measurement.c",
    ):
        require(expected in joined, f"target source missing {expected}")
    require("stm32f10x_i2c.c" not in joined, "hardware-I2C SPL source linked")
    for relative, expected in EXPECTED_APP_HASHES.items():
        require(sha256(FW / "App" / relative) == expected,
                f"Phase 1 public model changed: {relative}")
    for relative, expected in EXPECTED_PHASE2_HASHES.items():
        require(sha256(FW / relative) == expected,
                f"Phase 2 input drifted: {relative}")
    for relative, expected in EXPECTED_PHASE3_HASHES.items():
        require(sha256(FW / relative) == expected,
                f"Phase 3 input drifted: {relative}")
    print("PASS: Phase 1/2/3 validated input hashes")


def check_mapping() -> None:
    header = MEASUREMENT_H.read_text(encoding="utf-8")
    source = MEASUREMENT_C.read_text(encoding="utf-8")

    # 显式 mapping table 必须恰好 13 项。
    table_match = re.search(
        r"s_logical_cell_to_vc\[[^]]*\]\s*=\s*\{(.*?)\}", source, re.S)
    require(table_match is not None, "mapping table not found in C source")
    values = [int(v) for v in re.findall(r"\d+", table_match.group(1))]
    require(len(values) == 13, f"mapping table has {len(values)} entries, need 13")

    expected = [1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 15]
    require(values == expected, f"mapping table mismatch: {values}")
    require(9 not in values and 14 not in values,
            "VC9/VC14 exposed in mapping table")

    # VC register window 常量。
    require("BQ76940_MEASUREMENT_CELL_COUNT" in header, "cell count macro missing")
    require("BQ76940_MEASUREMENT_VC_WINDOW_BYTES" in header,
            "window size macro missing")

    # C 实现不得使用 VC1+index 的错误线性推导。
    require(re.search(r"BQ76940_REG_VC1_HI\s*\+\s*[^)]*logical", source) is None,
            "linear VC arithmetic found")
    require("s_logical_cell_to_vc[index]" in source,
            "table lookup not used for cell index")
    print("PASS: 13S mapping oracle (VC9/VC14 skipped, 13 golden entries)")


def check_cell_oracle() -> None:
    """Golden vectors must match the embedded C harness expectations."""
    source = MEASUREMENT_C.read_text(encoding="utf-8")
    header = MEASUREMENT_H.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase4_measurement.c").read_text(encoding="utf-8")

    # 换算必须复用唯一公式路径，不能复制第二套实现。
    require("BSP_BQ76940_ConvertCellRawToMv" in source,
            "cell conversion does not reuse Phase 3 conversion")

    # harness golden cell mV 必须匹配 GAIN=380/OFFSET=+30 的独立计算。
    golden = [2365, 2462, 2559, 2657, 2754, 2851, 2948,
              3046, 3240, 3338, 3435, 3532, 3727]
    raw14s = [0x1800, 0x1900, 0x1A00, 0x1B00, 0x1C00, 0x1D00, 0x1E00,
              0x1F00, 0x2100, 0x2200, 0x2300, 0x2400, 0x2600]
    vcs = [1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 15]
    for vc, raw, expected in zip(vcs, raw14s, golden):
        uv = raw * 380 + 30 * 1000
        mv = (uv + 500) // 1000
        require(mv == expected, f"oracle mismatch for VC{vc}: {mv} != {expected}")
    for vc, raw, expected in zip(vcs, raw14s, golden):
        require(f"{expected}U" in test or str(expected) in test,
                f"golden {expected} mV not present in harness")
    require("GOLD_CELL_MV" in test, "golden cell array missing in harness")
    print("PASS: cell conversion oracle (TI eq. 1, GAIN=380 OFFSET=+30)")


def check_bat_oracle() -> None:
    source = MEASUREMENT_C.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase4_measurement.c").read_text(encoding="utf-8")

    # TI eq.9：V(BAT)=4*GAIN*ADC + cell-count*OFFSET。
    vectors = [(0x0000, 390), (0x4E20, 30790), (0xFFFF, 100003)]
    for bat_raw, expected in vectors:
        uv = 4 * 380 * bat_raw + 13 * 30 * 1000
        mv = (uv + 500) // 1000
        require(mv == expected, f"BAT oracle mismatch 0x{bat_raw:04X}")
    for _, expected in vectors:
        require(str(expected) in test, f"BAT golden {expected} missing in harness")
    require("BQ76940_MEASUREMENT_BAT_NUM_CELLS" in MEASUREMENT_H.read_text(
        encoding="utf-8"), "BAT cell count macro missing")
    print("PASS: BAT pack-voltage oracle (TI eq. 9, 13 cells)")


def check_cc_oracle() -> None:
    source = MEASUREMENT_C.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase4_measurement.c").read_text(encoding="utf-8")

    require("BSP_BQ76940_DecodeSigned16" in source,
            "CC raw does not reuse Phase 3 signed decode")

    # TI eq.3：CC raw×8.44 uV/LSB；电流除法按 ARMCC5 C 语义向零截断。
    vectors = [(0, 0), (1, 2), (32767, 69138), (-32768, -69140),
               (-1, -2), (10000, 21100), (-10000, -21100)]
    for raw, expected in vectors:
        ma = int(raw * 8440 / 4000)
        require(ma == expected, f"CC oracle mismatch raw={raw}: {ma} != {expected}")
    for _, expected in vectors:
        require(str(expected) in test, f"CC golden {expected} missing in harness")
    require("BQ76940_MEASUREMENT_CC_LSB_NV" in MEASUREMENT_H.read_text(
        encoding="utf-8"), "CC LSB macro missing")
    print("PASS: CC raw decode + CC->mA oracle (TI eq. 3, truncating)")


def check_ts_oracle() -> None:
    source = MEASUREMENT_C.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase4_measurement.c").read_text(encoding="utf-8")

    # TI eq.4/5：VTS=raw×382 uV；RTS=10000×VTS/(3.3V−VTS)。
    vectors = [(0x0000, 0), (0x0A00, 4211), (0x1000, 9016)]
    for raw, expected in vectors:
        vts = raw * 382
        r = (10000 * vts) // (3300000 - vts)
        require(r == expected, f"TS oracle mismatch 0x{raw:04X}: {r}")
    for _, expected in vectors:
        require(str(expected) in test, f"TS golden {expected} missing in harness")
    # 0x27DC 令 VTS>=3.3 V，必须返回 RANGE_ERROR。
    vts = 0x27DC * 382
    require(vts >= 3300000, "TS over-range oracle wrong")
    require("0x27DCU" in test, "TS over-range vector missing in harness")
    print("PASS: TS1 raw->resistance oracle (TI eq. 4/5)")


def check_boundaries() -> None:
    source = MEASUREMENT_C.read_text(encoding="utf-8")
    header = MEASUREMENT_H.read_text(encoding="utf-8")

    # Driver 不得 include RTOS/App/task header，也不得写 g_bms_data。
    for token in ("FreeRTOS", "task.h", "queue.h", "semphr.h", "event_groups.h"):
        require(token not in header and token not in source,
                f"RTOS header leaked into measurement driver: {token}")
    require("g_bms_data" not in source and "g_bms_data" not in header,
            "measurement driver writes global snapshot")
    for token in ("bms_data.h", "bms_state.h", "bms_fault.h", "bms_protect.h",
                  "bms_soc.h", "app_tasks.h"):
        require(token not in header and token not in source,
                f"App header leaked into measurement driver: {token}")

    # measurement driver 不实现 protection/balance/SOC/CAN/ALERT。
    for token in ("ProtectTask", "SampleTask", "StateTask", "SOCTask",
                  "BalanceTask", "CAN_", "SYS_STAT", "CELLBAL", "SYS_CTRL2",
                  "OV_TRIP", "UV_TRIP", "EXTI", "vTaskStartScheduler"):
        require(token not in source,
                f"forbidden Phase 5+/RTOS symbol in measurement driver: {token}")

    # required API 必须存在。
    for api in ("BSP_BQ76940_ReadCellVoltages13", "BSP_BQ76940_ReadPackVoltageMv",
                "BSP_BQ76940_ReadCcRaw", "BSP_BQ76940_ConvertCcRawToCurrentMa",
                "BSP_BQ76940_ReadTs1Raw", "BSP_BQ76940_ConvertTs1RawToResistanceOhm",
                "BSP_BQ76940_Measurement_VcChannelOfLogicalCell"):
        require(api in header, f"public API missing: {api}")

    print("PASS: Phase 4 driver boundary (no RTOS/App/protection/CAN/HAL)")


def check_execution() -> None:
    simulator = SIM_LOG.read_text(encoding="utf-8", errors="replace")
    require("PHASE4_TEST_COMPLETED=1" in simulator, "C harness did not complete")
    require("PHASE4_TEST_FAILURES=0" in simulator, "C harness reported failures")
    require("*** error" not in simulator.lower(), "simulator command error")

    test_map = TEST_MAP.read_text(encoding="utf-8", errors="replace")
    for token in (
        "ARM Compiler 5.06 update 7 (build 960)",
        "bq76940_measurement.o", "bq76940.o", "crc8_bq76940.o",
        "test_phase4_mapping.o", "test_phase4_measurement.o",
    ):
        require(token in test_map, f"actual-C test map missing {token}")

    require(TEST_AXF.stat().st_mtime >= max(p.stat().st_mtime for p in TEST_SOURCES),
            "Phase 4 test AXF is stale relative to its sources")
    require(SIM_LOG.stat().st_mtime >= TEST_AXF.stat().st_mtime,
            "Phase 4 simulator log is stale relative to test AXF")
    print("PASS: ARMCC5 Simulator actual-C execution (completed=1 failures=0)")


def main() -> int:
    checks = (
        ("Phase 1/2/3 validated input hashes", check_regression),
        ("13S mapping oracle", check_mapping),
        ("cell conversion oracle", check_cell_oracle),
        ("BAT pack-voltage oracle", check_bat_oracle),
        ("CC raw/current oracle", check_cc_oracle),
        ("TS1 resistance oracle", check_ts_oracle),
        ("Phase 4 driver boundary", check_boundaries),
        ("ARMCC5 Simulator execution", check_execution),
    )
    try:
        for name, function in checks:
            function()
    except (AssertionError, FileNotFoundError, ET.ParseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("PHASE4_STATIC_AND_EXECUTION_CHECKS: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
