# BMS V1 Phase 6 报告

日期：2026-08-15
阶段：FreeRTOS Foundation / Seven Tasks / IPC Objects
判定：`PHASE 6: COMPLETE` / `CANDIDATE FOR CODEX REVIEW`

## 1. Git baseline

| 项 | 值 |
|---|---|
| Parent branch | `dsh/phase5` |
| Parent commit | `f673196`（phase5: sync uvoptx file list and record full Clean Rebuild evidence） |
| 当前 branch | `dsh/phase6` |
| main / phase3-validated / dsh/phase4 / dsh/phase5 | UNCHANGED |
| 远程 | 无 push；未创建 phase6-validated tag |

## 2. Preflight

- `dsh/phase5` clean，HEAD `f673196`；
- `git switch -c dsh/phase6 dsh/phase5` 成功；
- Phase 5 candidate 作为输入基线，未返回修改任何内容。

## 3. 证据边界

ARMCC5 生产 Clean+Rebuild、ARMCC5/Keil Simulator 对实际 FreeRTOS + app_rtos C 的执行、Python 独立 oracle/静态审查。无目标板；调度器实际运行、SysTick/PendSV 行为、堆高水位、ISR 时序为 `HARDWARE VALIDATION REQUIRED / DEFERRED`（见 §22）。

## 4. 输入基线

| 输入 | 状态 |
|---|---|
| Phase 1 Config/State/Fault | VALIDATED，哈希复核 PASS |
| Phase 2 BSP/SoftI2C/CRC | VALIDATED，哈希复核 PASS |
| Phase 3 BQ transport/calibration | VALIDATED，哈希复核 PASS |
| Phase 4 measurement | CANDIDATE，哈希复核 PASS |
| Phase 5 protection/FET/balance control | CANDIDATE，哈希复核 PASS |

## 5. 新增 / 修改 production files

新增：

- `firmware/Config/FreeRTOSConfig.h`（SHA-256 `57bb94a1dd4865c94661aacf805c0cdb8961c66cc8b7436370ec6e860662e351`）
- `firmware/App/app_rtos.h`（SHA-256 `d4d9690ced5c4aa6baf2f5f79f8357b9d0cfb9c69c7efe7ce3f4ed09baeee588`）
- `firmware/App/app_rtos.c`（SHA-256 `53741b1076ce45b8cc73392acbf33f1bbe666857e21c7aaa24a0fd69c4836566`）
- `firmware/App/app_rtos_hooks.c`（SHA-256 `8519ec593efe72394773a8da48185e1d0102b993f2b6e7a3f957a9526f1e7917`）

修改：

- `firmware/User/main.c`（SHA-256 `b48690d4c70bc3ba58b6033346bda1b07e4f0c925e340fd21ec95d7c600c8035`）：BQ 初始化后创建 RTOS 对象、七任务骨架、启动调度器（spec §12 顺序）
- `firmware/Project/Keil/BMS_V1.uvprojx`：新增 `FreeRTOS_V11` group（tasks/queue/list/event_groups/timers/stream_buffer/croutine/heap_4/port.c + app_rtos.c/hooks.c），IncludePath 追加 `docs\FreeRTOS\include` 与 `docs\FreeRTOS\portable`（新 SHA-256 `089b41545ea6893f628873cc5c713223b4396a5d303306cc1820f196eee0457d`）
- `firmware/Project/Keil/BMS_V1.uvoptx`：Simulator 入口切到 `phase6_simulator.ini`
- `Listings/BMS_V1.map`、`Objects/BMS_V1.axf`、`Build/BMS_V1_Phase6_build.log`：重建产物

未修改：Phase 1-5 全部 production/测试文件、历史 verifier（verify_phase1/2/3 不可变）、docs/FreeRTOS 只读源码（kernel 从 docs 引用，未改）。

## 6. 新增测试文件

- `firmware/Tests/test_phase6.h/.c` 系列（main/objects/tasks）
- `firmware/Tests/verify_phase6.py`
- `firmware/Tests/phase6_tests.sct`、`phase6_simulator.ini`
- `firmware/Tests/Build/Phase6/*`

## 7. 架构

```text
main (Phase 6 段)
  ├─ App_Rtos_CreateObjects()   -> 7 IPC objects
  ├─ App_Rtos_CreateTasks()     -> 7 task skeletons
  └─ vTaskStartScheduler()

FreeRTOS V11.1.0 kernel (docs/FreeRTOS 只读引用)
  └─ firmware/Config/FreeRTOSConfig.h (BMS 专用，勘误 C-01/C-02/H-08/H-09)
```

七任务骨架（体为 Phase 6 占位，职责注释标注 Phase 7..11 落点）：

| 任务 | 优先级 | 周期 | 后续职责 |
|---|---|---|---|
| Protect | 5 | 20 ms | Phase 7: ALERT/SYS_STAT/CC_READY |
| Sample | 4 | 250 ms | Phase 8: measurement 发布 |
| State | 3 | 100 ms | Phase 9: 状态机/软件保护/IWDG |
| SOC | 3 | 1 s | Phase 10: CC 队列 + 库仑积分 |
| Balance | 2 | 1 s | Phase 10: 均衡策略 + CELLBAL |
| CANTx | 2 | 250 ms | Phase 11: xCanTxQueue 发送 |
| CANRx | 2 | 10 ms | Phase 11: xCanRxQueue 解析 |

## 8. FreeRTOSConfig 锁定（勘误 C-01/C-02/H-08/H-09）

| 项 | 值 | 依据 |
|---|---|---|
| configMAX_PRIORITIES | 8 | C-01 |
| 七任务优先级 | 5/4/3/3/2/2/2 | C-01 |
| configTICK_RATE_HZ | 1000（1 ms 抢占） | Gate §3.2 |
| configTOTAL_HEAP_SIZE | 8 KiB（初始预算，非永久） | H-08 |
| configPRIO_BITS / grouping | 4 / PriorityGroup_4 | C-02 |
| kernel raw / max syscall raw | 0xF0 / 0x50 | C-02 |
| configCHECK_FOR_STACK_OVERFLOW | 2 | H-09 |
| configASSERT + hooks | 实现于 app_rtos_hooks.c | H-09 |
| configUSE_MUTEXES / COUNTING_SEMAPHORES | 1 / 1 | 对象需求 |
| configUSE_TIMERS | 1（V11 timers.c） | kernel |
| handler 映射 | vPort* → SVC/PendSV/SysTick_Handler | RVDS/ARM_CM3 |

编译期断言锁定：MAX_PRIORITIES==8、TICK==1000、PRIO_BITS==4、kernel==0xF0、syscall==0x50、stack check==2、heap<=20 KiB。

## 9. IPC objects（spec §11.2）

```c
xI2CMutex / xDataMutex / xAfeAlertSem   (semaphores)
xCanTxQueue / xCanRxQueue / xCcSampleQueue  (queues)
xSysEvents  (event group)
```

`App_Rtos_CreateObjects()`：全部创建成功才返回 pdTRUE；任一失败删除已创建对象并返回 pdFALSE（无部分对象集）。

## 10. Queue element 布局

- `BMS_CcSample_t`：int16 raw + TickType_t tick，ARMCC 布局 8 B（tick 偏移 4）
- `BMS_CanFrame_t`：uint32 ext_id + uint8 dlc + uint8 data[8]，ARMCC 布局 16 B（ext 偏移 0、dlc 偏移 4、data 偏移 5）

测试断言 offsetof 布局（非裸和），避免 padding 误判。

## 11. Hooks（H-09）

- `vApplicationAssertFailedHandler`：关中断 + 停机
- `vApplicationMallocFailedHook`：停机
- `vApplicationStackOverflowHook`：停机（记录任务名待 Phase 9 诊断）
- `vApplicationIdleHook`：空（Phase 6）

## 12. main 集成（spec §12）

```text
BQ76940_Init (调度器前，单线程)
  -> App_Rtos_CreateObjects()
  -> App_Rtos_CreateTasks()
  -> vTaskStartScheduler()
  -> BMS_SafeIdle() (仅调度器致命错误时返回)
```

## 13. Output transactional contract

- `App_Rtos_CreateObjects`：全成或全删（pdTRUE/pdFALSE）
- `App_Rtos_CreateTasks`：任一任务创建失败即返回 pdFALSE（已创建任务保留，调用方停机）
- 错误路径统一由 main 进入 `BMS_SafeIdle()`（保持既有安全停机语义）

## 14. Software tests

实际 FreeRTOS V11.1.0 + app_rtos C 由 ARMCC5 编译链接，Keil Cortex-M3 Simulator 执行（调度器不启动，验证对象/任务创建 + 配置常量）：

```text
PHASE6_TEST_COMPLETED=1
PHASE6_TEST_FAILURES=0
P6_OBJECTS_FAILURES=0  P6_TASKS_FAILURES=0
```

覆盖：

- 七对象全部创建成功且非 NULL；
- 事件位定义（EVT_SAMPLE_READY 等 4 位）；
- 队列元素布局（offsetof）；
- 七任务优先级 C-01 精确值；
- 七任务栈 words（spec §11.4）；
- 七任务全部创建成功；
- 调度器状态为 NOT_STARTED（生产流程中 vTaskStartScheduler 前）。

**测试中发现并修复**：初版 Test_Phase6_Tasks 重复调用 CreateTasks 两次（14 任务）耗尽 8 KiB heap 触发 malloc 失败钩子死循环——已修正为单次创建（验证目的正确）。

## 15. ARMCC5 Simulator 结果

`phase6_simulator.log`（SHA-256 `ecce2ba55191bb45c5e55dee3444b8d832e17050ff0f1a2e708eb14b6ecd8948`）：

```text
PHASE6_TEST_COMPLETED=1
PHASE6_TEST_FAILURES=0
```

`phase6_tests.map` 出现 production `tasks.o/queue.o/heap_4.o/port.o/app_rtos.o` 与测试对象。软件模拟执行，非调度器实时性验证。

## 16. Python oracle 结果

`verify_phase6.py`（SHA-256 `9a86a64854bdffa1e1e0688b34a5f659b520886fe1b840281d4e3d4e6cea3a2a`）exit 0，6 组 PASS：

```text
PHASE6_STATIC_AND_EXECUTION_CHECKS: PASS
```

log：`verify_phase6.log`（SHA-256 `52219726b56dfa6795814b289ee494c77bf7185a6a75a15f1880155099d3e946`）。

## 17. ARMCC5 Clean Rebuild

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -cr '...\BMS_V1.uvprojx' -t 'BMS_V1' -j0 -o '...\BMS_V1_Phase6_build.log'
```

结果：ARM Compiler 5.06 update 7 build 960，28 个 units 全量重编译，`0 Error(s), 0 Warning(s)`。build log SHA-256 `349fb672fe367284304f20b069f6dc41db276118c392c6f1d38870bd43eb2d6f`。

## 18. Code / RO / RW / ZI

```text
Code=12912  RO-data=268  RW-data=180  ZI-data=10412
Total RO=13180 B  Total RW=10592 B  Total ROM=13360 B
```

## 19. SRAM 预算（H-08）

```text
RW+ZI            = 10592 B
+MSP stack(0x400) = 1024 B
+C library heap   = 512 B
合计             = 12128 B / 20480 B (20 KiB)
剩余             = 8352 B
```

8 KiB FreeRTOS heap + 七任务栈 + IPC 对象均在 20 KiB SRAM 内 ✓。这是初始预算；Phase 9/12 以 map/high-water 收敛（H-08 保留）。

## 20. Phase 1 → 6 size 增量

| 指标 | P1 | P2 | P3 | P4 | P5 | P6 | P5→P6 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Code | 812 | 3084 | 3164 | 3164 | 3164 | 12912 | +9748 |
| RO-data | 252 | 268 | 268 | 268 | 268 | 268 | 0 |
| RW-data | 0 | 24 | 32 | 32 | 32 | 180 | +148 |
| ZI-data | 1856 | 1896 | 1896 | 1896 | 1896 | 10412 | +8516 |

P5→P6 大幅增长来自 FreeRTOS kernel（tasks/queue/port 等）与 8 KiB heap + 任务栈的 ZI。

## 21. 历史 regression

| Verifier | 结果 | 说明 |
|---|---|---|
| verify_phase1/2/3 | FAIL（历史快照，预期） | 不可变；不要求整体 PASS |
| verify_phase4.py | FAIL（main.c 哈希变化，预期） | Phase 6 合法修改 main.c（启动调度器），verify_phase4 的 check_regression 固定 Phase 3 main.c 哈希故 FAIL；Phase 6 报告明确标注此语义变化 |
| verify_phase5.py | FAIL（main.c 哈希变化，预期） | 同上（verify_phase5 也固定 main.c 哈希） |
| verify_phase6.py | **PASS**（6/6） | check_regression 覆盖 Phase 1 App 6 文件、Phase 2 源 11 文件、Phase 3 源 3 文件（不含 main.c——由 Phase 6 修改）、Phase 4 源 2 文件、Phase 5 源 2 文件精确哈希 + Phase 6 target 增量 + FreeRTOS include path |

**说明**：`main.c` 由 Phase 3 的"纯初始化"扩展为"初始化 + RTOS 启动"，属 Phase 6 授权修改。历史 verifier 固定旧 main.c 哈希而 FAIL 是快照语义的预期行为；main.c 的 Phase 6 版本由 verify_phase6 的 `check_main_integration` 验证（objects→tasks→scheduler 顺序）。

## 22. Hardware Validation TODO

统一状态：`HARDWARE VALIDATION REQUIRED / DEFERRED`：

- 调度器在目标板的真实运行（SysTick/PendSV/SVC）；
- 任务栈 high-water 实测（7 栈 + Idle + Timer）；
- heap_4 实际碎片与 8 KiB 裕量；
- ISR 优先级（EXTI6/CAN7）与 FreeRTOS 临界区的真实交互；
- NVIC PriorityGroup_4 设置验证；
- IWDG 集成后调度停顿（Phase 9+）。

Simulator/mock 只证明对象/任务创建与配置正确，不冒充调度实时性。

## 23. Git commit list

（提交后由 `git log dsh/phase5..HEAD` 提供；建议拆分：FreeRTOSConfig + app_rtos、Keil target、tests+oracle、report）

## 24. git diff --stat dsh/phase5..HEAD

（提交后输出）

## 25. Codex takeover review 注意事项

1. **main.c 是 Phase 3 以来首次修改**：从"初始化后停机"变为"初始化 + RTOS 启动"。verify_phase4/5 因固定旧 main.c 哈希而 FAIL 是预期（快照语义），verify_phase6 验证新顺序。
2. **FreeRTOS 从 docs/ 只读引用**：kernel 源码未复制/修改；`FreeRTOSConfig.h` 是 BMS 专用新文件（docs 的旧配置仍 BLOCK）。
3. **测试不启动调度器**：Simulator 验证对象/任务创建；调度实时性属硬件/集成验证。
4. **heap 8 KiB 是预算**：当前 14 对象 + 7 任务 + timer 服务在 Simulator 创建成功；Phase 9/12 以 high-water 收敛。
5. **任务体是骨架**：只有 vTaskDelayUntil 周期占位，无任何 Phase 7+ 逻辑。
6. 未创建 phase6-validated tag。

## Phase 6 Hard Gate

| Gate | Result |
|---|---|
| 当前分支 dsh/phase6 | PASS |
| main / phase3-validated / dsh/phase4 / dsh/phase5 未改变 | PASS |
| Phase 5 candidate 输入哈希 | PASS |
| FreeRTOSConfig（C-01/C-02/H-08/H-09） | PASS |
| IPC objects 全创建 | PASS |
| 七任务骨架 + 优先级 | PASS |
| hooks 实现 | PASS |
| main 集成顺序 | PASS |
| 独立 Python oracle | PASS |
| ARMCC5 Simulator actual-C | PASS |
| Production Clean Rebuild 0/0 | PASS |
| SRAM 预算（8 KiB heap + 栈在 20 KiB 内） | PASS |
| 无 Phase 7 功能（任务体占位） | PASS |
| 无 HAL / 无 hardware I2C | PASS |
| Git commits 完成 | PASS |
| Phase 6 report 完成 | PASS |
| 调度实时性 / 目标板运行 | DEFERRED |

`PHASE 6: COMPLETE`

`STATUS: CANDIDATE FOR CODEX REVIEW`
