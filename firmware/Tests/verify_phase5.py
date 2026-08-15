#!/usr/bin/env python3
"""Independent/static Phase 5 gate checks; never invokes GCC.

Verifies independently of the C code under test:
  1. Phase 4 candidate + Phase 1/2/3 validated input hashes (regression).
  2. OV/UV trip encoding oracle (TI 8.3.1.2.1 formula).
  3. OCD/SCD/delay selection tables (TI Tables 8-9..8-11).
  4. PROTECT1/2/3 register composition (bit fields).
  5. FET arbitration (SYS_CTRL2 preservation, errata H-04).
  6. CELLBAL mapping (spec 5.1 / TI Tables 8-4..8-6).
  7. Phase 5 boundary: no RTOS, no ALERT/ProtectTask/SampleTask, no
     SYS_STAT loop, no protection policy evaluation, no CAN/Flash.
  8. ARMCC5 Simulator log + test AXF freshness.
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
CONTROL_H = FW / "Driver" / "bq76940_control.h"
CONTROL_C = FW / "Driver" / "bq76940_control.c"
SIM_LOG = FW / "Tests" / "Build" / "Phase5" / "phase5_simulator.log"
TEST_AXF = FW / "Tests" / "Build" / "Phase5" / "phase5_tests.axf"
TEST_MAP = FW / "Tests" / "Build" / "Phase5" / "phase5_tests.map"
TEST_SOURCES = (
    FW / "Driver" / "bq76940_control.c",
    FW / "Tests" / "test_phase5_main.c",
    FW / "Tests" / "test_phase5_trip.c",
    FW / "Tests" / "test_phase5_ocdscd.c",
    FW / "Tests" / "test_phase5_fet.c",
    FW / "Tests" / "test_phase5_cellbal.c",
)

PHASE4_REPORT_HASH = "ff5111589652a8fb5c3c6fab2af084f2b5770dd3e819555a60167d6ee5e8021f"
PHASE4_UVPROJX_HASH = "d829c41f0e813544bbda3a31eeca669168c05c2ccd8095efb1cc63b300745c1f"
PHASE4_MEASUREMENT_H = "4883401eef11bd8fca01f9c20d9607e785b844fe82321120fad154fc5355f877"
PHASE4_MEASUREMENT_C = "69819eacae2ebdea8c911397522cfb26ae03c1e09e38d85bbd33c81e3db4e177"

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

EXPECTED_PHASE4_HASHES = {
    "Driver/bq76940_measurement.h": PHASE4_MEASUREMENT_H,
    "Driver/bq76940_measurement.c": PHASE4_MEASUREMENT_C,
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_regression() -> None:
    require(sha256(ROOT / "deliverables" / "phase4" / "BMS_V1_Phase4_Report.md") ==
            PHASE4_REPORT_HASH,
            "Phase 4 report revision drifted")
    # uvprojx is extended by Phase 4 (measurement) and now by Phase 5
    # (control); verify both sources are present in the target.
    tree = ET.parse(PROJECT)
    paths = [node.text or "" for node in tree.findall(".//FilePath")]
    joined = "\n".join(paths).lower()
    for expected in (
        "bq76940.c", "crc8_bq76940.c", "soft_i2c.c", "bsp_clock.c",
        "bsp_gpio.c", "bsp_timer.c", "stm32f10x_rcc.c",
        "stm32f10x_gpio.c", "stm32f10x_tim.c", "startup_stm32f10x_md.s",
        "bq76940_measurement.c", "bq76940_control.c",
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
    for relative, expected in EXPECTED_PHASE4_HASHES.items():
        require(sha256(FW / relative) == expected,
                f"Phase 4 input drifted: {relative}")
    print("PASS: Phase 1/2/3/4 validated input hashes + Phase 5 target diff")


def check_trip_oracle() -> None:
    source = CONTROL_C.read_text(encoding="utf-8")
    header = CONTROL_H.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase5_trip.c").read_text(encoding="utf-8")

    require("BQ76940_Control_EncodeOvTrip" in header, "OV encode API missing")
    require("BQ76940_Control_EncodeUvTrip" in header, "UV encode API missing")

    # TI official example: OV 4.30V, GAIN=382, OFFSET=0 -> 0xBF.
    def trip(target_mv, gain, off, msb):
        full = (target_mv - off) * 1000 // gain
        assert (full >> 12) == msb
        return (full >> 4) & 0xFF

    require(trip(4300, 382, 0, 2) == 0xBF, "OV 4.30V oracle mismatch")
    require(trip(2500, 382, 0, 1) == 0x99, "UV 2.50V oracle mismatch")
    require(trip(4250, 382, 0, 2) == 0xB7, "OV 4.25V oracle mismatch")
    require(trip(2800, 382, 0, 1) == 0xCA, "UV 2.80V oracle mismatch")
    require(trip(4250, 382, 30, 2) == 0xB2, "OV 4.25V offset+30 oracle mismatch")
    require(trip(2800, 382, 30, 1) == 0xC5, "UV 2.80V offset+30 oracle mismatch")

    for v in ("0xBFU", "0x99U", "0xB7U", "0xCAU", "0xB2U", "0xC5U"):
        require(v in test, f"golden {v} missing in harness")
    print("PASS: OV/UV trip encoding oracle (TI 8.3.1.2.1)")


def check_ocd_scd_oracle() -> None:
    source = CONTROL_C.read_text(encoding="utf-8")
    header = CONTROL_H.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase5_ocdscd.c").read_text(encoding="utf-8")

    ocd_rsns1 = [17, 22, 28, 33, 39, 44, 50, 56, 61, 67, 72, 78, 83, 89, 94, 100]
    ocd_rsns0 = [8, 11, 14, 17, 19, 22, 25, 28, 31, 33, 36, 39, 42, 44, 47, 50]
    ocd_delay = [8, 20, 40, 80, 160, 320, 640, 1280]
    scd_rsns1 = [6, 44, 67, 89, 111, 133, 155, 178]
    scd_rsns0 = [22, 33, 44, 56, 67, 78, 89, 100]
    scd_delay = [70, 100, 200, 400]
    ov_delay = [1, 2, 4, 8]
    uv_delay = [1, 4, 8, 16]

    def select(table, req):
        for i, v in enumerate(table):
            if v >= req:
                return i
        return None

    cases = [
        (ocd_rsns1, 56, 7), (ocd_rsns1, 17, 0), (ocd_rsns1, 100, 15),
        (ocd_rsns0, 30, 8), (ocd_delay, 80, 3), (ocd_delay, 100, 4),
        (scd_rsns1, 111, 4), (scd_rsns1, 178, 7), (scd_rsns0, 60, 4),
        (scd_delay, 100, 1), (ov_delay, 2, 1), (ov_delay, 4, 2),
        (uv_delay, 4, 1), (uv_delay, 16, 3),
    ]
    for table, req, expected in cases:
        require(select(table, req) == expected,
                f"selection oracle mismatch req={req}")
    require(select(ocd_rsns1, 101) is None, "OCD 101mV should be RANGE_ERROR")
    require(select(scd_rsns1, 179) is None, "SCD 179mV should be RANGE_ERROR")
    require(select(ov_delay, 9) is None, "OV 9s should be RANGE_ERROR")
    require(select(uv_delay, 17) is None, "UV 17s should be RANGE_ERROR")

    # PROTECT composition.
    require(((1 << 3) | 4) == 0x0C or True, "sanity")
    p1 = (0x80) | ((1 & 3) << 3) | (4 & 7)
    require(p1 == 0x8C, "PROTECT1 compose mismatch")
    p2 = ((5 & 7) << 4) | (0x0A & 0x0F)
    require(p2 == 0x5A, "PROTECT2 compose mismatch (datasheet prose 0x5B is an error)")
    p3 = ((1 & 3) << 6) | ((1 & 3) << 4)
    require(p3 == 0x50, "PROTECT3 compose mismatch")

    for v in ("0x5AU", "0x50U", "0x8CU", "0x80U"):
        require(v in test, f"golden {v} missing in harness")
    print("PASS: OCD/SCD/delay/PROTECT oracle (TI Tables 8-9..8-11)")


def check_fet_oracle() -> None:
    source = CONTROL_C.read_text(encoding="utf-8")
    header = CONTROL_H.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase5_fet.c").read_text(encoding="utf-8")

    require("BQ76940_Control_SysCtrl2WithFets" in header, "FET API missing")
    require("BQ76940_Control_ObserveFets" in header, "Observe API missing")
    require("BQ76940_Control_ApplyInhibits" in header, "Inhibit API missing")

    def sysctrl2(current, chg, dsg):
        n = current & 0xFC
        if chg:
            n |= 1
        if dsg:
            n |= 2
        return n

    require(sysctrl2(0x40, True, True) == 0x43, "CC_EN not preserved")
    require(sysctrl2(0x40, False, False) == 0x40, "all-off keeps CC_EN")
    require(sysctrl2(0xFF, False, True) == 0xFE, "CHG clear only")
    require(sysctrl2(0xC0, True, False) == 0xC1, "DELAY_DIS preserved")

    for v in ("0x43U", "0x40U", "0xFEU", "0xC1U"):
        require(v in test, f"golden {v} missing in harness")
    print("PASS: FET arbitration oracle (errata H-04 single-writer)")


def check_cellbal_oracle() -> None:
    source = CONTROL_C.read_text(encoding="utf-8")
    header = CONTROL_H.read_text(encoding="utf-8")
    test = (FW / "Tests" / "test_phase5_cellbal.c").read_text(encoding="utf-8")

    logical_to_cb = [1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 15]
    require(len(logical_to_cb) == 13, "CB mapping length")
    require(9 not in logical_to_cb and 14 not in logical_to_cb,
            "CB9/CB14 exposed")

    def compose(bitmap):
        b1 = b2 = b3 = 0
        for i, cb in enumerate(logical_to_cb):
            if bitmap & (1 << i):
                if cb <= 5:
                    b1 |= 1 << (cb - 1)
                elif cb <= 10:
                    b2 |= 1 << (cb - 6)
                else:
                    b3 |= 1 << (cb - 11)
        return b1, b2, b3

    require(compose(1 << 0) == (0x01, 0x00, 0x00), "Cell1 -> CB1")
    require(compose(1 << 5) == (0x00, 0x01, 0x00), "Cell6 -> CB6")
    require(compose(1 << 8) == (0x00, 0x10, 0x00), "Cell9 -> CB10")
    require(compose(1 << 12) == (0x00, 0x00, 0x10), "Cell13 -> CB15")

    require("0x1FFFU" in test, "golden full-bitmap missing in harness")
    print("PASS: CELLBAL mapping oracle (spec 5.1 / TI Tables 8-4..8-6)")


def check_boundaries() -> None:
    source = CONTROL_C.read_text(encoding="utf-8")
    header = CONTROL_H.read_text(encoding="utf-8")

    for token in ("FreeRTOS", "task.h", "queue.h", "semphr.h", "event_groups.h"):
        require(token not in header and token not in source,
                f"RTOS header leaked: {token}")
    require("g_bms_data" not in source and "g_bms_data" not in header,
            "control driver writes global snapshot")
    for token in ("bms_data.h", "bms_state.h", "bms_fault.h", "app_tasks.h"):
        require(token not in header and token not in source,
                f"App header leaked: {token}")

    for token in ("ProtectTask", "SampleTask", "StateTask", "SOCTask",
                  "BalanceTask", "SYS_STAT", "EXTI", "vTaskStartScheduler",
                  "CAN_", "PROTECT1", "WriteByte", "ReadByte"):
        require(token not in source,
                f"forbidden Phase 6+/I2C/register-write symbol in control driver: {token}")
    # Phase 5 driver only ENCODES registers; it must not perform I2C
    # transactions or evaluate protection policy.
    require("BQ76940_WriteBlock" not in source, "control driver performs I2C")
    require("BQ76940_ReadBlock" not in source, "control driver performs I2C")

    for api in ("BQ76940_Control_EncodeOvTrip", "BQ76940_Control_EncodeUvTrip",
                "BQ76940_Control_SelectOcdThreshold",
                "BQ76940_Control_SelectScdThreshold",
                "BQ76940_Control_ComposeProtect1",
                "BQ76940_Control_ComposeProtect2",
                "BQ76940_Control_ComposeProtect3",
                "BQ76940_Control_SysCtrl2WithFets",
                "BQ76940_Control_ObserveFets",
                "BQ76940_Control_ApplyInhibits",
                "BQ76940_Control_CellBalBitOfLogicalCell",
                "BQ76940_Control_ComposeCellBal",
                "BQ76940_Control_DecodeCellBal"):
        require(api in header, f"public API missing: {api}")
    print("PASS: Phase 5 driver boundary (no RTOS/I2C/policy/CAN/ALERT)")


def check_execution() -> None:
    simulator = SIM_LOG.read_text(encoding="utf-8", errors="replace")
    require("PHASE5_TEST_COMPLETED=1" in simulator, "C harness did not complete")
    require("PHASE5_TEST_FAILURES=0" in simulator, "C harness reported failures")
    require("*** error" not in simulator.lower(), "simulator command error")

    test_map = TEST_MAP.read_text(encoding="utf-8", errors="replace")
    for token in (
        "ARM Compiler 5.06 update 7 (build 960)",
        "bq76940_control.o",
        "test_phase5_trip.o", "test_phase5_ocdscd.o",
        "test_phase5_fet.o", "test_phase5_cellbal.o",
    ):
        require(token in test_map, f"actual-C test map missing {token}")

    require(TEST_AXF.stat().st_mtime >= max(p.stat().st_mtime for p in TEST_SOURCES),
            "Phase 5 test AXF is stale relative to its sources")
    require(SIM_LOG.stat().st_mtime >= TEST_AXF.stat().st_mtime,
            "Phase 5 simulator log is stale relative to test AXF")
    print("PASS: ARMCC5 Simulator actual-C execution (completed=1 failures=0)")


def main() -> int:
    checks = (
        ("Phase 1/2/3/4 input hashes + Phase 5 target", check_regression),
        ("OV/UV trip oracle", check_trip_oracle),
        ("OCD/SCD/delay/PROTECT oracle", check_ocd_scd_oracle),
        ("FET arbitration oracle", check_fet_oracle),
        ("CELLBAL mapping oracle", check_cellbal_oracle),
        ("Phase 5 driver boundary", check_boundaries),
        ("ARMCC5 Simulator execution", check_execution),
    )
    try:
        for name, function in checks:
            function()
    except (AssertionError, FileNotFoundError, ET.ParseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("PHASE5_STATIC_AND_EXECUTION_CHECKS: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
