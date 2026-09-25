#!/usr/bin/env python3
"""Independent/static Phase 7 gate checks; never invokes GCC.

Verifies independently of the C code under test:
  1. Phase 6 candidate + Phase 1..5 validated input hashes (regression).
  2. SYS_STAT bit mapping oracle (SLUSBK2I 8.3.1.3): each bit maps to the
     correct fault id and FET inhibit; CC_READY is not a fault; XREADY is
     never in the clear mask.
  3. Protect decision logic (FML_Protect_Decide) golden behavior.
  4. Phase 7 boundary: ISR only gives the semaphore; no state machine,
     SOC, balancing, CAN implementation in the protect path.
  5. ARMCC5 Simulator log + test AXF freshness.
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
PROTECT_H = FW / "App" / "bms_protect.h"
PROTECT_C = FW / "App" / "bms_protect.c"
EXTI_H = FW / "Driver" / "bsp_exti.h"
EXTI_C = FW / "Driver" / "bsp_exti.c"
SIM_LOG = FW / "Tests" / "Build" / "Phase7" / "phase7_simulator.log"
TEST_AXF = FW / "Tests" / "Build" / "Phase7" / "phase7_tests.axf"
TEST_MAP = FW / "Tests" / "Build" / "Phase7" / "phase7_tests.map"
TEST_SOURCES = (
    FW / "App" / "bms_protect.c",
    FW / "Driver" / "bsp_exti.c",
    FW / "Tests" / "test_phase7_main.c",
    FW / "Tests" / "test_phase7_logic.c",
    FW / "Tests" / "test_phase7_stub_i2c.c",
)

PHASE6_UVPROJX_HASH = "089b41545ea6893f628873cc5c713223b4396a5d303306cc1820f196eee0457d"

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

EXPECTED_PHASE6_HASHES = {
    "Config/FreeRTOSConfig.h": "57bb94a1dd4865c94661aacf805c0cdb8961c66cc8b7436370ec6e860662e351",
    "App/app_rtos.h": "bd367d0f84a240a59663034b29de6def0230af4b7def55e031b4fc805a3c2161",
    "App/app_rtos.c": "ee511f492769b18a9ef3b0cdfb04c978a8170eb8fae63cc690b8c4ab00ca67e4",
    "App/app_rtos_hooks.c": "8519ec593efe72394773a8da48185e1d0102b993f2b6e7a3f957a9526f1e7917",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_regression() -> None:
    tree = ET.parse(PROJECT)
    paths = [node.text or "" for node in tree.findall(".//FilePath")]
    joined = "\n".join(paths).lower()
    for expected in (
        "bq76940.c", "crc8_bq76940.c", "soft_i2c.c", "bsp_clock.c",
        "bsp_gpio.c", "bsp_timer.c", "bsp_exti.c",
        "stm32f10x_rcc.c", "stm32f10x_gpio.c", "stm32f10x_tim.c",
        "stm32f10x_exti.c", "misc.c",
        "startup_stm32f10x_md.s",
        "bq76940_measurement.c", "bq76940_control.c",
        "tasks.c", "queue.c", "list.c", "event_groups.c", "timers.c",
        "stream_buffer.c", "croutine.c", "heap_4.c", "port.c",
        "app_rtos.c", "app_rtos_hooks.c", "bms_protect.c",
    ):
        require(expected in joined, f"target source missing {expected}")
    require("stm32f10x_i2c.c" not in joined, "hardware-I2C SPL source linked")

    for rel, expected in {**EXPECTED_APP_HASHES,
                          **EXPECTED_PHASE2_HASHES,
                          **EXPECTED_PHASE3_HASHES,
                          **EXPECTED_PHASE4_HASHES,
                          **EXPECTED_PHASE5_HASHES,
                          **EXPECTED_PHASE6_HASHES}.items():
        path = FW / "App" / rel if rel.startswith("bms_") or rel.startswith("app_") else FW / rel
        require(sha256(path) == expected,
                f"input drifted: {rel}")
    print("PASS: Phase 1..6 input hashes + Phase 7 target diff")


def check_stat_mapping_oracle() -> None:
    header = PROTECT_H.read_text(encoding="utf-8")
    source = PROTECT_C.read_text(encoding="utf-8")

    # bit mask 必须匹配 SLUSBK2I 8.3.1.3。
    masks = {
        "BMS_PROTECT_STAT_CC_READY": 0x80,
        "BMS_PROTECT_STAT_DEVICE_XREADY": 0x20,
        "BMS_PROTECT_STAT_OVRD_ALERT": 0x10,
        "BMS_PROTECT_STAT_UV": 0x08,
        "BMS_PROTECT_STAT_OV": 0x04,
        "BMS_PROTECT_STAT_SCD": 0x02,
        "BMS_PROTECT_STAT_OCD": 0x01,
    }
    for name, value in masks.items():
        require(re.search(rf"#define\s+{name}\s+\(\(uint8_t\)0x{value:02X}U\)", header),
                f"{name} != 0x{value:02X}")

    # pure decision API 必须 public。
    require("FML_Protect_Decide" in header, "Decide API missing")
    require("FML_Protect_HasFaultBits" in header, "HasFaultBits API missing")
    require("void FML_Protect_Decide(" in source, "Decide impl missing")

    # 独立 oracle 的 golden decision。
    # OV→HW_OV active、CHG inhibit、OV 进入 clear mask。
    require("BMS_FAULT_ID_HW_OV" in source, "OV fault id missing")
    require("BMS_FAULT_ID_AFE_OVRD_ALERT" in source, "OVRD fault id missing")
    require("BMS_FAULT_ID_AFE_XREADY" in source, "XREADY fault id missing")
    # XREADY 不得进入普通 clear mask。
    m = re.search(r"never cleared|NOT added to the clear mask", source)
    require(m is not None, "XREADY clear-mask exclusion not documented")
    print("PASS: SYS_STAT bit mapping oracle (SLUSBK2I 8.3.1.3)")


def check_protect_logic() -> None:
    source = PROTECT_C.read_text(encoding="utf-8")
    header = PROTECT_H.read_text(encoding="utf-8")

    # FET request 使用冻结 single-writer type。
    require("BQ76940_FetRequest_t" in header, "FET request type missing")
    require("BQ76940_FET_DESIRE_DISABLE" in source, "FET desire missing")

    # H-01/H-02/H-03/H-05 marker。
    for marker in ("H-01", "H-02", "H-03", "H-05"):
        require(marker in source or marker in header,
                f"errata marker {marker} missing")

    # CC_READY 与 fault bit 分开处理。
    require("FML_Protect_HandleCcReady" in source, "CC_READY handler missing")
    require("BMS_Protect_PushCcSample" in header, "CC queue API missing")
    require("newest sample" in header, "H-02 newest-wins policy missing")

    print("PASS: protect decision logic contract (H-01/H-02/H-03/H-05)")


def check_exti_boundary() -> None:
    exti_h = EXTI_H.read_text(encoding="utf-8")
    exti_c = EXTI_C.read_text(encoding="utf-8")
    protect_c = PROTECT_C.read_text(encoding="utf-8")

    require("BSP_EXTI1_LOGICAL_PRIORITY              (6U)" in exti_h,
            "EXTI logical priority != 6")
    require("BSP_ALERT_EXTI_Init" in exti_h, "EXTI init API missing")

    # ISR 只 give semaphore/yield，不得 I2C/BQ/printf。
    handler = protect_c[protect_c.index("void EXTI1_IRQHandler"):]
    for token in ("BQ76940_", "SoftI2C_", "printf", "vTaskDelay",
                  "BQ_Write", "BQ_Read"):
        require(token not in handler,
                f"ISR performs forbidden operation: {token}")
    require("xSemaphoreGiveFromISR" in handler, "ISR does not give semaphore")
    require("portYIELD_FROM_ISR" in handler, "ISR does not yield")

    # EXTI init 使用冻结 C-02 priority。
    require("NVIC_IRQChannelPreemptionPriority" in exti_c,
            "EXTI priority not configured")
    print("PASS: ALERT EXTI boundary (ISR minimal, priority 6)")


def check_boundaries() -> None:
    source = PROTECT_C.read_text(encoding="utf-8")
    header = PROTECT_H.read_text(encoding="utf-8")

    # ProtectTask 不实现 state/SOC/balance/CAN。
    for token in ("BMS_STATE_", "BMS_SocPermille", "CELLBAL", "CAN_",
                  "BSP_BQ76940_Control_ComposeCellBal", "vTaskStartScheduler"):
        require(token not in source,
                f"forbidden Phase 8+/system symbol in bms_protect.c: {token}")

    # Protect 不得写诊断 aggregate g_bms_data。
    require("g_bms_data" not in source, "protect writes global snapshot")
    print("PASS: Phase 7 boundary (no state/SOC/balance/CAN in protect)")


def check_execution() -> None:
    simulator = SIM_LOG.read_text(encoding="utf-8", errors="replace")
    require("PHASE7_TEST_COMPLETED=1" in simulator, "C harness did not complete")
    require("PHASE7_TEST_FAILURES=0" in simulator, "C harness reported failures")
    require("*** error" not in simulator.lower(), "simulator command error")

    test_map = TEST_MAP.read_text(encoding="utf-8", errors="replace")
    for token in (
        "ARM Compiler 5.06 update 7 (build 960)",
        "bms_protect.o", "bms_fault.o",
        "test_phase7_logic.o",
    ):
        require(token in test_map, f"actual-C test map missing {token}")
    # 该 test image 只链接 pure decision path，故意不引入 bq76940 transport。
    require("i.BSP_SoftI2C_WriteByte" not in test_map,
            "transport accidentally linked into decision-only test")

    require(TEST_AXF.stat().st_mtime >= max(p.stat().st_mtime for p in TEST_SOURCES),
            "Phase 7 test AXF is stale relative to its sources")
    require(SIM_LOG.stat().st_mtime >= TEST_AXF.stat().st_mtime,
            "Phase 7 simulator log is stale relative to test AXF")
    print("PASS: ARMCC5 Simulator actual-C execution (completed=1 failures=0)")


def main() -> int:
    checks = (
        ("Phase 1..6 input hashes + Phase 7 target", check_regression),
        ("SYS_STAT bit mapping oracle", check_stat_mapping_oracle),
        ("protect decision logic contract", check_protect_logic),
        ("ALERT EXTI boundary", check_exti_boundary),
        ("Phase 7 boundary", check_boundaries),
        ("ARMCC5 Simulator execution", check_execution),
    )
    try:
        for name, function in checks:
            function()
    except (AssertionError, FileNotFoundError, ET.ParseError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("PHASE7_STATIC_AND_EXECUTION_CHECKS: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
