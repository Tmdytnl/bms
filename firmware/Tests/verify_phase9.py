#!/usr/bin/env python3
"""Phase 9 simulation-development gate.

This gate deliberately verifies software architecture, ARMCC5 simulator
evidence, and the target build. It is separate from verify_phase8.py, whose
frozen contract remains the real-hardware/evidence qualification gate.
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FAILURES: list[str] = []


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        FAILURES.append(f"missing artifact: {relative}")
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def check(condition: bool, description: str) -> None:
    if condition:
        print(f"PASS: {description}")
    else:
        print(f"FAIL: {description}")
        FAILURES.append(description)


def contains_all(text: str, fragments: tuple[str, ...]) -> bool:
    return all(fragment in text for fragment in fragments)


baseline = ROOT / "docs/hardware/BMS_V1_模拟硬件参数与产品策略基线.md"
check(baseline.is_file(), "SIM-HW-POLICY-V1 baseline exists")

policy_h = read("firmware/Config/bms_policy.h")
policy_c = read("firmware/Config/bms_policy.c")
check("SIM_POLICY_V1" in policy_h, "central profile identifies SIM_POLICY_V1")
check("const BMS_Policy_t *BMS_Policy_Get(void)" in policy_c,
      "central immutable policy getter exists")
check("bool BMS_Policy_Validate" in policy_c,
      "compiled policy has fail-closed structural validation")

required_sources = {
    "..\\..\\App\\bms_state.c",
    "..\\..\\App\\bms_hw_recovery.c",
    "..\\..\\App\\bms_recovery.c",
    "..\\..\\App\\bms_fet_manager.c",
    "..\\..\\App\\bms_health.c",
    "..\\..\\App\\bms_soc.c",
    "..\\..\\App\\bms_balance.c",
    "..\\..\\App\\bms_can.c",
    "..\\..\\App\\bms_persistence.c",
    "..\\..\\Driver\\bsp_iwdg.c",
    "..\\..\\Driver\\bsp_can.c",
    "..\\..\\Driver\\bsp_flash.c",
    "..\\..\\Driver\\bsp_uart.c",
    "..\\..\\..\\docs\\reference\\ST\\STM32F10x Standard Peripheral Library\\Libarary\\stm32f10x_can.c",
    "..\\..\\..\\docs\\reference\\ST\\STM32F10x Standard Peripheral Library\\Libarary\\stm32f10x_flash.c",
    "..\\..\\..\\docs\\reference\\ST\\STM32F10x Standard Peripheral Library\\Libarary\\stm32f10x_usart.c",
}
project_path = ROOT / "firmware/Project/Keil/BMS_V1.uvprojx"
project_sources: set[str] = set()
if project_path.is_file():
    try:
        tree = ET.parse(project_path)
        project_sources = {
            (node.text or "").strip()
            for node in tree.findall(".//FilePath")
        }
    except ET.ParseError as exc:
        FAILURES.append(f"invalid Keil project XML: {exc}")
check(required_sources <= project_sources,
      "production Keil project includes every Phase 9 module")

app_rtos = read("firmware/App/app_rtos.c")
state_h = read("firmware/App/bms_state.h")
state_c = read("firmware/App/bms_state.c")
data_h = read("firmware/App/bms_data.h")
data_c = read("firmware/App/bms_data.c")
protect_c = read("firmware/App/bms_protect.c")
recovery_h = read("firmware/App/bms_recovery.h")
recovery_c = read("firmware/App/bms_recovery.c")
sample_c = read("firmware/App/bms_sample.c")
fet_c = read("firmware/App/bms_fet_manager.c")
health_h = read("firmware/App/bms_health.h")
balance_c = read("firmware/App/bms_balance.c")
soc_c = read("firmware/App/bms_soc.c")
can_c = read("firmware/App/bms_can.c")
persistence_c = read("firmware/App/bms_persistence.c")
can_driver = read("firmware/Driver/bsp_can.c")
flash_driver = read("firmware/Driver/bsp_flash.c")
uart_driver = read("firmware/Driver/bsp_uart.c")

check(contains_all(data_h, ("sample_sequence", "afe_generation",
                            "BMS_DataIdentity_t")) and
      "BMS_Data_GetIdentity" in data_c,
      "measurement publication exposes sequence and AFE generation identity")
check(contains_all(state_h, ("evaluated_sample_sequence",
                             "evaluated_afe_generation")) and
      contains_all(state_c, ("BMS_State_PublishIfCurrent",
                              "BMS_Data_GetIdentity")),
      "State decisions compare-and-publish against measurement identity")

phases = (
    "PRE_CLEAR_PREPARE", "PRE_CLEAR_READY", "WAIT_CLEAR_ACK",
    "POST_CLEAR_CONFIG", "POST_CLEAR_SETTLE", "POST_CLEAR_VERIFY",
    "CALIBRATION_HANDOFF", "WAIT_FIRST_VALID_SAMPLE", "COMPLETE", "FAILED",
)
check(contains_all(recovery_h, phases),
      "XREADY recovery exposes the complete phaseful protocol")
check(contains_all(recovery_c, ("BMS_Sample_InvalidateCalibrationForXready",
                                "BMS_Sample_SetRecoveryCalibration",
                                "sample_sequence", "afe_generation")),
      "Recovery invalidates calibration and proves first valid generation")
check(contains_all(sample_c, ("s_recovery_provenance_required",
                              "evidence->xready_generation",
                              "evidence->recovery_revision",
                              "evidence->post_clear_verified")),
      "runtime calibration is provenance-bound and cached rebinding is denied")

protect_sysctrl2_write = re.search(
    r"BQ76940_WriteByte\s*\([^;]*BQ76940_REG_SYS_CTRL2", protect_c, re.S)
check(protect_sysctrl2_write is None,
      "Protect has no scheduler-era SYS_CTRL2 write")
check("BMS_FetManager_Service" in app_rtos and
      contains_all(fet_c, ("BQ76940_REG_SYS_CTRL2", "ReadByte", "WriteByte",
                           "UNVERIFIED", "QUARANTINED")),
      "State invokes the sole transactional scheduler-era FET manager")
check("BQ76940_REG_SYS_CTRL2" not in recovery_c,
      "Recovery delegates scheduler-era SYS_CTRL2 ownership to FET manager")
check("BMS_Data_GetSnapshot" not in fet_c and
      contains_all(fet_c, ("BMS_Protect_GetSafetySnapshot",
                           "BMS_State_GetSafetySnapshot")),
      "FET authority comes from Protect/State, not diagnostic BMS_Data")

other_runtime_app = "\n".join(
    read(str(path.relative_to(ROOT)).replace("\\", "/"))
    for path in (ROOT / "firmware/App").glob("*.c")
    if path.name not in {"bms_protect.c", "bms_afe_startup.c"}
)
other_xready_w1c = re.search(
    r"BQ76940_WriteByte\s*\([^;]*BQ76940_REG_SYS_STAT[^;]*"
    r"BMS_PROTECT_STAT_DEVICE_XREADY", other_runtime_app, re.S)
check(other_xready_w1c is None and
      "BMS_PROTECT_STAT_DEVICE_XREADY" in protect_c,
      "Protect is the sole runtime XREADY W1C owner")

iwdg_calls_outside_state = "\n".join(
    read(str(path.relative_to(ROOT)).replace("\\", "/"))
    for path in (ROOT / "firmware").rglob("*.c")
    if path.as_posix().endswith("firmware/App/app_rtos.c") is False and
       path.as_posix().endswith("firmware/Driver/bsp_iwdg.c") is False and
       "Tests" not in path.parts
)
check(contains_all(app_rtos, ("BSP_IWDG_StartNominal", "BSP_IWDG_Feed")) and
      not re.search(r"BSP_IWDG_(?:StartNominal|Feed)\s*\(",
                    iwdg_calls_outside_state),
      "StateTask is the sole production IWDG starter/feeder")
check(contains_all(health_h, ("generation[BMS_HEALTH_TASK_COUNT]",
                              "observed_advance", "watchdog_armed")) and
      "clear" not in "\n".join(
          line for line in health_h.splitlines()
          if line.strip().startswith(("void BMS_Health_", "bool BMS_Health_"))),
      "health uses monotonic per-task generations with no clear API")
check("BMS_Balance_RunOnce" in app_rtos and
      contains_all(balance_c, ("BQ76940_REG_CELLBAL1",
                               "BQ76940_REG_CELLBAL2",
                               "BQ76940_REG_CELLBAL3")) and
      "BQ76940_REG_CELLBAL" not in recovery_c,
      "BalanceTask is the sole scheduler-era CELLBAL writer")
check(contains_all(soc_c, ("sample.xready_generation",
                           "remaining_mams", "queue_gap_latched")) and
      not re.search(r"\b(?:float|double)\b", soc_c),
      "SOC is integer-only and binds CC samples to AFE generation")
check("BMS_Protect_SubmitServiceResetRequest" in can_c and
      "BQ76940_WriteByte" not in can_c and
      "BSP_IWDG_Feed" not in can_c and
      "fault_has_direct_fet_effect" not in can_c,
      "CAN commands route only through source-specific service requests")
check(contains_all(can_driver, ("CAN_Prescaler = BSP_CAN_PRESCALER",
                                "CAN_BS1_6tq", "CAN_BS2_1tq",
                                "CAN_FilterMode_IdMask",
                                "BSP_CAN_RX_LOGICAL_PRIORITY")) and
      contains_all(can_c, ("USB_LP_CAN1_RX0_IRQHandler",
                            "xQueueSendFromISR",
                            "BMS_Can_TxHardwareService")),
      "target bxCAN binding has locked timing, filter and ISR/task ownership")
check(contains_all(persistence_c, ("BMS_Persistence_Crc32",
                                   "BMS_Persistence_SelectNewest",
                                   "BMS_PERSISTENCE_COMMIT_OFFSET",
                                   "BMS_Persistence_StoreSocIfDue",
                                   "BMS_Persistence_TargetServiceSoc")) and
      contains_all(flash_driver, ("BMS_PARAM_A_ADDR",
                                   "BMS_PARAM_B_ADDR",
                                   "FLASH_ErasePage",
                                   "FLASH_ProgramHalfWord")),
      "Flash A/B physical persistence is commit-last and page-restricted")
check(contains_all(uart_driver, ("GPIO_Pin_9", "GPIO_Pin_10",
                                 "BSP_UART1_BAUDRATE",
                                 "USART_WordLength_8b",
                                 "USART_StopBits_1",
                                 "USART_Parity_No")),
      "required debug UART1 binding is PA9/PA10 at 115200 8N1")

all_production = "\n".join(
    read(str(path.relative_to(ROOT)).replace("\\", "/"))
    for directory in ("firmware/App", "firmware/User", "firmware/Driver")
    for path in (ROOT / directory).glob("*.c")
)
check(not re.search(r"clear_all_latched|ClearAllLatched|"
                    r"fault_latched_bitmap\s*=\s*0", all_production),
      "generic fault-latch clear is absent")

phase9_build = read("firmware/Tests/Build/Phase9/phase9_build.log")
phase9_sim = read("firmware/Tests/Build/Phase9/phase9_simulator.log")
check("PHASE9_TEST_IMAGE_BUILD_PASS" in phase9_build and
      "Error:" not in phase9_build and "Warning:" not in phase9_build,
      "Phase 9 ARMCC5 test image builds without warnings/errors")
for marker in (
    "PHASE9_TEST_COMPLETED=1", "PHASE9_TEST_FAILURES=0",
    "P9_LOGIC_FAILURES=0", "P9_FET_FAILURES=0",
    "P9_RECOVERY_FAILURES=0", "P9_HEALTH_FAILURES=0",
    "P9_HW_HANDSHAKE_FAILURES=0", "P9_SCENARIOS_COMPLETED=24",
    "P9_RACES_COMPLETED=3", "CONTINUATION_TEST_COMPLETED=1",
    "CONTINUATION_TEST_FAILURES=0", "P10_SOC_FAILURES=0",
    "P10_BALANCE_FAILURES=0", "P11_CAN_FAILURES=0",
    "STORAGE_CODEC_FAILURES=0", "CONTINUATION_SCENARIOS_COMPLETED=8",
    "STRESS_TEST_COMPLETED=1", "STRESS_TEST_FAILURES=0",
    "STRESS_ITERATIONS=50000", "STRESS_SIMULATED_MS=600000000",
    "STRESS_PERSISTENCE_TRANSACTIONS=512",
):
    check(marker in phase9_sim, f"simulator evidence contains {marker}")

phase8_sim = read("firmware/Tests/Build/Phase8/phase8_split_simulator.log")
nonzero_regression_failures = [
    f"{name}={value}"
    for name, value in re.findall(r"^([A-Z0-9_]*FAILURES)=(\d+)\s*$",
                                  phase8_sim, re.M)
    if value != "0"
]
check(not nonzero_regression_failures and
      "P7_SIM_COMM_FAILURES=0" in phase8_sim and
      "PHASE8_SAMPLE_TEST_FAILURES=0" in phase8_sim and
      "P8_SAMPLE_PROVENANCE_GUARD_COMPLETED=1" in phase8_sim,
      "Phase 4/6/7/8 ARMCC5 simulator regression is green")

production_log = read("firmware/Project/Keil/Build/BMS_V1_Phase8_build.log")
check('0 Error(s), 0 Warning(s)' in production_log and
      all(f"compiling {source}..." in production_log for source in (
          "bms_state.c", "bms_recovery.c", "bms_fet_manager.c",
          "bms_health.c", "bms_hw_recovery.c", "bms_soc.c",
          "bms_balance.c", "bms_can.c", "bms_persistence.c",
          "bsp_iwdg.c", "bsp_can.c", "bsp_flash.c", "bsp_uart.c",
          "stm32f10x_can.c", "stm32f10x_flash.c",
          "stm32f10x_usart.c")),
      "production ARMCC5 Clean/Rebuild is Phase 9-complete at 0/0")

production_map = read(
    "firmware/Tests/Build/Phase8/BMS_V1_Phase8_production.map")
check("Max: 0x0000f400" in production_map and
      "0x0800F800UL" in policy_c and "0x0800FC00UL" in policy_c,
      "linker boundary excludes both proposed final 1 KiB Flash pages")
rw_match = re.search(r"Total RW\s+Size \(RW Data \+ ZI Data\)\s+"
                     r"(\d+)", production_map)
check(rw_match is not None and int(rw_match.group(1)) < (20 * 1024),
      "measured static RAM plus 12 KiB RTOS heap fits 20 KiB SRAM")

if FAILURES:
    print(f"PHASE9 SIMULATION GATE FAIL ({len(FAILURES)} failure(s))")
    sys.exit(1)

print("HARDWARE_CLAIM=NONE")
print("PHASE9 SIMULATION GATE PASS")
