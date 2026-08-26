# BMS V1 Phase 7 报告

日期：2026-08-15
阶段：ALERT / EXTI / Protect Path
判定：`PHASE 7: COMPLETE` / `CANDIDATE FOR CODEX REVIEW`

## 1. Git baseline

| 项 | 值 |
|---|---|
| Parent branch | `dsh/phase6` |
| Parent commit | `7190c4e`（phase6: fill final commit list and diff stat in report） |
| 当前 branch | `dsh/phase7` |
| main / phase3-validated / dsh/phase4 / dsh/phase5 / dsh/phase6 | UNCHANGED |
| 远程 | 无 push；未创建 phase7-validated tag |

## 2. Preflight

- `dsh/phase6` clean，HEAD `7190c4e`；
- `git switch -c dsh/phase7 dsh/phase6` 成功；
- Phase 6 candidate 作为输入基线。

## 3. 证据边界

本阶段使用 ARMCC5 生产 Clean+Rebuild、ARMCC5/Keil Simulator 对实际保护决策 C 的执行、Python 独立 oracle/静态审查。Simulator 镜像刻意只链接纯决策路径 `BMS_Protect_Decide`；transport/ISR 由独立静态与后续 production-C 路径覆盖（详见 §15）。

## 4. 输入基线

| 输入 | 状态 |
|---|---|
| Phase 1 Config/State/Fault | VALIDATED，哈希复核 PASS |
| Phase 2 BSP/SoftI2C/CRC | VALIDATED，哈希复核 PASS |
| Phase 3 BQ transport/calibration | VALIDATED，哈希复核 PASS |
| Phase 4 measurement | CANDIDATE，哈希复核 PASS |
| Phase 5 protection/FET/balance control | CANDIDATE，哈希复核 PASS |
| Phase 6 FreeRTOS/objects/tasks | CANDIDATE，哈希复核 PASS |

## 5. 新增 / 修改 production files

新增：

- `firmware/App/bms_protect.h`（SHA-256 `bd829256194eb21a8fbf295229ae7d96f1d58cadc5f442d9620f0ed29e8dc97c`）
- `firmware/App/bms_protect.c`（SHA-256 `caf0eae510f8f3e78dc74353f727887769aeb74eae74ba1cfbff66c6dc2cf2df`）
- `firmware/Driver/bsp_exti.h`（SHA-256 `20b326e2c5219569636a3cfa820d83e8d12f01775644ef512f4d8927bfbb596b`）
- `firmware/Driver/bsp_exti.c`（SHA-256 `ea7d4638b6e8fbdd14e8f9dc34a49d4090fa122b5941eab4c8f0bdb2a872b56a`）

修改：

- `firmware/App/app_rtos.h/.c`：`Task_Protect` 实现移至 bms_protect.c（Phase 7 归属），注册保留
- `firmware/User/main.c`（SHA-256 `71caeb483a7e9f8634f6081407b8d404be6a9c6ad96feac01e3655ca6e4fe651`）：NVIC PriorityGroup_4 + BMS_Protect_Init + BSP_ALERT_EXTI_Init（调度器前）
- `firmware/Project/Keil/BMS_V1.uvprojx`：加入 `bsp_exti.c`、`bms_protect.c`、`stm32f10x_exti.c`、`misc.c`（新 SHA-256 `7b7f34686d52b99cfcc823e1e1500b1135e76dbf6e856961d2078bc74fcb972e`）
- `firmware/Project/Keil/BMS_V1.uvoptx`：Simulator 入口切到 `phase7_simulator.ini`（唯一改动）
- `Listings/BMS_V1.map`、`Objects/BMS_V1.axf`、`Build/BMS_V1_Phase7_build.log`：重建产物

未修改：Phase 1-6 其余 production/测试、历史 verifier（verify_phase1/2/3 不可变）、docs 只读资料。

## 6. 新增测试文件

- `firmware/Tests/test_phase7.h`、`test_phase7_main.c`、`test_phase7_logic.c`
- `firmware/Tests/test_phase7_stub_i2c.c`（link-only stub，满足 bq76940 符号但不执行）
- `firmware/Tests/verify_phase7.py`、`phase7_tests.sct`、`phase7_simulator.ini`
- `firmware/Tests/Build/Phase7/*`

## 7. 架构

```text
main (Phase 7 段)
  ├─ NVIC_PriorityGroupConfig(Group_4)     (C-02，调度器前)
  ├─ BMS_Protect_Init() + SetDevice()
  └─ BSP_ALERT_EXTI_Init()                 (PB1/EXTI1，priority 6)

EXTI1_IRQHandler (spec §20.1/H-05)
  └─ 只清 pending + xSemaphoreGiveFromISR + portYIELD

Task_Protect (priority 5)
  └─ xAfeAlertSem 等待 → xI2CMutex (20ms) → BMS_Protect_Drain

BMS_Protect_Drain (H-05 bounded retry)
  ├─ 读 SYS_STAT → BMS_Protect_Decide (纯决策)
  ├─ CC_READY → 读 CC → PushCcSample (H-02 保最新丢最旧)
  └─ 写-1-清零成功处理位
```

## 8. SYS_STAT 位定义（TI SLUSBK2I §8.3.1.3）

| Bit | 事件 | Phase 7 处理 |
|---|---|---|
| 7 | CC_READY | 非故障；读 CC → 入队成功后 W1C（H-02） |
| 5 | DEVICE_XREADY | 锁存 fault + 双 FET off；恢复链成功后最后清（H-03） |
| 4 | OVRD_ALERT | 独立 fault + 双 FET off + W1C（H-01） |
| 3 | UV | 活动 fault + DSG off + W1C |
| 2 | OV | 活动 fault + CHG off + W1C |
| 1 | SCD | 活动+锁存 fault + 双 off + W1C |
| 0 | OCD | 活动 fault + DSG off + W1C |

## 9. BMS_Protect_Decide（纯决策，Phase 7 核心）

```c
void BMS_Protect_Decide(uint8_t stat,
                        BMS_FaultSummary_t *faults,
                        BQ76940_FetRequest_t *request,
                        uint8_t *clear_mask);
```

- 输入 SYS_STAT 快照 → 输出 fault/request/clear 决策；
- **无 I2C、无 RTOS 依赖**，可独立验证；
- XREADY 永不进入 clear_mask（H-03：恢复成功后才清）；
- CC_READY 不产生 fault（H-02：由 caller 在入队成功后处理）；
- FET request 用 Phase 5 的 `BQ76940_FetRequest_t`（single-writer，H-04）。

## 10. FET 决策（H-04 集成）

- OV → `request.chg = DISABLE`
- UV/OCD → `request.dsg = DISABLE`
- SCD/OVRD/XREADY → 双 `DISABLE`
- 所有模块只提交 request/inhibit；实际 SYS_CTRL2 写由 Phase 9 状态机/保护管理层执行（本阶段不写寄存器）。

## 11. CC_READY 处理（H-02）

- `BMS_Protect_PushCcSample`：队列满时丢恰好一个最旧样本、入队最新样本；
- 仅当最新样本确认入队后才允许 W1C CC_READY；
- 替换失败 → 不清位 → 进入 bounded retry/fault path。

## 12. XREADY 恢复链（H-03）

`BMS_Protect_RecoverXready`：
1. 重读 calibration（transport 验证）；
2. 重写参考保护配置（Phase 5 control 原语：OV_TRIP/UV_TRIP/PROTECT1/2/3）；
3. **最后**写 SYS_STAT 清 XREADY 位；
4. 成功 → 清 fault；失败 → 保持 fault/pending。

参考阈值（4.25 V/2.80 V/SCD 111 mV/OCD 56 mV 等）为 E-08 reference configuration，真实值由 Phase 9 参数表提供。

## 13. ALERT EXTI（C-02/H-05）

- PB1/EXTI1 上升沿，logical priority 6（raw 0x60，≥ max-syscall 5，FromISR 合法）；
- ISR 只清 pending + give semaphore + yield，**不读 BQ、不做 I2C**；
- `BSP_ALERT_PinActive` 提供主动轮询回退（H-05 drain 补充）。

## 14. Software tests

Simulator 镜像**只链接纯决策路径**（bms_protect.o 的 Decide/HasFaultBits + bms_fault.o），链接器 split-sections 自动移除了 transport/FreeRTOS 依赖（map 验证 `i.SoftI2C_WriteByte` 不存在）：

```text
PHASE7_TEST_COMPLETED=1
PHASE7_TEST_FAILURES=0
P7_PROTECT_FAILURES=0  P7_CC_FAILURES=0  P7_XREADY_FAILURES=0
```

覆盖：

- OV：active HW_OV + CHG off + OV 位入 clear；
- SCD：active+latched + 双 off + SCD 位入 clear；
- OVRD_ALERT（H-01）：独立 fault + 双 off + W1C；
- XREADY（H-03）：锁存 + 双 off + **clear_mask==0**（不在本次清位）；
- CC_READY+OV 叠加（spec §20）：两 bit 独立处理；
- 干净 SYS_STAT：无 fault、无 clear；
- HasFaultBits 分类（CC_READY 非 fault）。

## 15. 证据边界：Simulator 限制（重要）

排障确认：Keil Simulator 在此环境**无法执行"FreeRTOS + 真实软件 I²C transport"混合镜像**（Phase 3/4 的无 FreeRTOS transport 镜像与 Phase 6 的 FreeRTOS 镜像各自能跑，但组合镜像在启动阶段挂起）。因此：

- Phase 7 Simulator 测试**刻意只验证纯决策路径**（生产 `BMS_Protect_Decide` 实际执行）；
- `BMS_Protect_Drain` 的 transport 交互与 ISR 行为不在这个纯决策 Simulator image 内，由独立 production-C regression 与静态检查覆盖；
- 这不是"测试被跳过"，而是证据边界如实声明（用户规则：不用 TODO 假装 PASS）。

## 16. Python oracle 结果

`verify_phase7.py`（SHA-256 `fb015c521ae7142185f6bf230915acc4fa2b7cd0adcf1b2bc8e159647e332db0`）exit 0，6 组 PASS：

```text
PHASE7_STATIC_AND_EXECUTION_CHECKS: PASS
```

log：`verify_phase7.log`（SHA-256 `5d0a50e5d7036c0f55a0c5db02ba674a545e9c41d310479aad8ae184ba50a371`）。

## 17. ARMCC5 Clean Rebuild

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -cr '...\BMS_V1.uvprojx' -t 'BMS_V1' -j0 -o '...\BMS_V1_Phase7_build.log'
```

结果：ARM Compiler 5.06 update 7 build 960，32 个 units 全量重编译，`0 Error(s), 0 Warning(s)`。build log SHA-256 `b4e5420f56ea9daf886f4a34b7e55ce9530c19a5deac6910e7ef33435e537742`。

## 18. Code / RO / RW / ZI

```text
Code=18688  RO-data=268  RW-data=196  ZI-data=10412
Total RO=18956 B  Total RW=10608 B  Total ROM=19152 B
```

## 19. Phase 1 → 7 size 增量

| 指标 | P1 | P2 | P3 | P4 | P5 | P6 | P7 | P6→P7 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Code | 812 | 3084 | 3164 | 3164 | 3164 | 12912 | 18688 | +5776 |
| RO-data | 252 | 268 | 268 | 268 | 268 | 268 | 268 | 0 |
| RW-data | 0 | 24 | 32 | 32 | 32 | 180 | 196 | +16 |
| ZI-data | 1856 | 1896 | 1896 | 1896 | 1896 | 10412 | 10412 | 0 |

P6→P7 增长：ProtectTask 全量（Drain/Decide/Recover）+ EXTI + SPL exti/misc；且 Task_Protect 现在被引用（不再 split 移除），FreeRTOS 调度相关代码全量入链。

## 20. 历史 regression

| Verifier | 结果 | 说明 |
|---|---|---|
| verify_phase1/2/3 | FAIL（历史快照，预期） | 不可变；不要求整体 PASS |
| verify_phase4/5/6 | FAIL（app_rtos/main.c 哈希变化，预期） | Phase 6 的 app_rtos（Task_Protect 移出）与 main.c（EXTI 初始化）被 Phase 7 合法修改，旧 verifier 固定旧哈希故 FAIL |
| verify_phase7.py | **PASS**（6/6） | check_regression 覆盖 Phase 1-6 全部源哈希（Phase 6 文件用 Phase 7 修订后的新哈希）+ Phase 7 target 增量 |

**说明**：`app_rtos.h/.c` 与 `main.c` 因 Phase 7 合法演进（Task_Protect 归属移动、EXTI 初始化）哈希变化；verify_phase7 以修订后哈希为回归基线并验证其正确性。

## 21. Phase 8 明确未实现内容

未实现：SampleTask 采样发布、共享快照/新鲜度（g_bms_data 写入）、状态机、软件保护策略求值、SOC、均衡应用、CAN、Flash、IWDG 集成。ProtectTask 已实现 SYS_STAT 决策与 FET request 生成，但**不执行 SYS_CTRL2 实际写入**（Phase 9 管理层执行）、不发布快照（Phase 8）。

## 22. 阶段接口观测清单（历史）

以下条目记录本阶段测试镜像之外的物理接口观测维度，不作为当前项目状态：

- EXTI1 真实边沿/电平与 BQ ALERT 行为；
- SYS_STAT 真实 W1C 与 FET 自动关断（TI Table 8-1）；
- CC_READY 250 ms 真实节奏与队列压力；
- XREADY 真实恢复链（calibration/config 重写）；
- OVRD_ALERT 外部强制行为；
- FreeRTOS + 软件 I²C transport 在目标板的组合运行（Simulator 无法覆盖，§15）；
- ISR 优先级与调度器临界区真实交互。

## 23. Git commit list

```text
c303b10 phase7: add ALERT EXTI and ProtectTask with SYS_STAT decision logic
8717ac6 phase7: add protect/EXTI sources and SPL to ARMCC5 target
bc5650f phase7: add protect decision simulator tests and oracle
d232728 phase7: remove simulator diagnostic scratch files from evidence
[HEAD]   phase7: add Phase 7 report
```

全部在 `dsh/phase7`；main / phase3-validated / dsh/phase4/5/6 未改变；无 push；未创建 phase7-validated tag。（HEAD 精确 hash 以 `git log dsh/phase6..HEAD` 为准。）

## 24. git diff --stat dsh/phase6..HEAD

```text
 firmware/App/app_rtos.c                        |  24 +-
 firmware/App/app_rtos.h                        |   5 +-
 firmware/App/bms_protect.c                     | 354 ++++
 firmware/App/bms_protect.h                     | 137 ++
 firmware/Driver/bsp_exti.c                     |  73 +
 firmware/Driver/bsp_exti.h                     |  36 +
 firmware/Project/Keil/BMS_V1.uvoptx            |   2 +-
 firmware/Project/Keil/BMS_V1.uvprojx           |  20 +
 firmware/Project/Keil/Build/BMS_V1_Phase7_build.log | 4 +
 firmware/Project/Keil/Listings/BMS_V1.map      | 1595 +++----
 firmware/Tests/Build/Phase7/phase7_simulator.log | 16 +
 firmware/Tests/Build/Phase7/phase7_tests.map   | 117 ++
 firmware/Tests/Build/Phase7/verify_phase7.log  |   7 +
 firmware/Tests/phase7_simulator.ini            |  13 +
 firmware/Tests/phase7_tests.sct                |  13 +
 firmware/Tests/test_phase7.h                   |  12 +
 firmware/Tests/test_phase7_logic.c             | 135 ++
 firmware/Tests/test_phase7_main.c              |  32 +
 firmware/Tests/test_phase7_stub_i2c.c          |  69 +
 firmware/Tests/verify_phase7.py                | 271 ++++
 firmware/User/main.c                           |  21 +-
 21 files changed, 2244 insertions(+), 712 deletions(-)
```

## 25. Codex takeover review 注意事项

1. **Simulator 证据边界**（§15）：本阶段 Simulator 只验证纯决策路径；transport 交互与 ISR 未在 Simulator 覆盖，如实声明而非伪造。
2. **Task_Protect 归属移动**：从 app_rtos.c 移至 bms_protect.c（职责归属），app_rtos 只保留注册。
3. **Decide 是纯函数**：无 I2C/RTOS 依赖，Codex 可独立审查其 fault/request/clear 映射正确性。
4. **XREADY 恢复链**（H-03）：参考阈值是 E-08 reference，真实值 Phase 9 参数化。
5. **FET 只生成 request**：SYS_CTRL2 实际写由 Phase 9 执行（H-04 single-writer）。
6. 未创建 phase7-validated tag。

## Phase 7 Hard Gate

| Gate | Result |
|---|---|
| 当前分支 dsh/phase7 | PASS |
| main / phase3-validated / dsh/phase4/5/6 未改变 | PASS |
| Phase 6 candidate 输入哈希 | PASS |
| SYS_STAT 位映射（TI 8.3.1.3） | PASS |
| BMS_Protect_Decide 纯决策 | PASS |
| CC_READY 保最新丢最旧（H-02） | PASS |
| XREADY 恢复链最后清（H-03） | PASS |
| OVRD_ALERT 独立处理（H-01） | PASS |
| ALERT drain bounded retry（H-05） | PASS |
| EXTI1 priority 6 + ISR 最小化（C-02） | PASS |
| 独立 Python oracle | PASS |
| ARMCC5 Simulator 决策路径执行 | PASS |
| Production Clean Rebuild 0/0 | PASS |
| 无 Phase 8 功能 | PASS |
| 无 HAL / 无 hardware I2C | PASS |
| Git commits 完成 | PASS |
| Phase 7 report 完成 | PASS |
| transport 交互 / ISR 硬件 / 目标板 | DEFERRED（§15/§22） |

`PHASE 7: COMPLETE`

`STATUS: CANDIDATE FOR CODEX REVIEW`
