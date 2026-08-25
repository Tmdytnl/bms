# BMS V1 M3 Resource Budget

Status: Engineering Closure M3 software baseline. Values below come from the
ARMCC5 5.06u7 production Clean/Rebuild map unless a row is explicitly marked
as a source-derived estimate. They are not target-board stack-watermark or
long-duration heap measurements.

## 1. Linked target budget

Target: `STM32F103C8T6`, 64 KiB physical Flash and 20 KiB SRAM. The linker
deliberately limits the application load region to `[0x08000000, 0x0800F400)`;
the final three 1 KiB pages are reserved for SOC (`0x0800F400`), parameter A
(`0x0800F800`) and parameter B (`0x0800FC00`).

| Item | M3 evidence | Limit | Remaining | Interpretation |
|---|---:|---:|---:|---|
| application load image | 54,800 B (`0xD610`) | 62,464 B (`0xF400`) | 7,664 B (12.27%) | includes Code + RO + initialized RW load image |
| Code | 53,344 B | — | — | ARMCC5 program-size line |
| RO data | 1,092 B | — | — | ARMCC5 program-size line |
| RW data | 364 B | — | — | initialized RAM and Flash load contribution |
| ZI data | 16,508 B | — | — | zero-initialized RAM |
| total RW + ZI | 16,872 B (`0x41E8`) | 20,480 B (`0x5000`) | 3,608 B (17.62%) | physical SRAM execution-region result |
| FreeRTOS `ucHeap` | 12,288 B | included above | — | `heap_4`; do **not** add it to RW+ZI again |
| non-`ucHeap` static RAM | 4,584 B | included above | — | difference only; includes MSP, C heap, globals and kernel workspace |
| startup MSP stack | 1,024 B | included above | — | startup/map allocation; not an RTOS task stack |
| C library heap | 512 B | included above | — | startup/map allocation; distinct from `ucHeap` |

The linked image fits both configured regions. This is a link/layout result,
not evidence that the exact MCU fitted to a board has the assumed density.

## 2. Task stacks and callgraph evidence

Task stacks are dynamically allocated from the already-counted 12 KiB
`ucHeap`. ARMCC5 callgraph maximum depth is useful static evidence, but it does
not replace `uxTaskGetStackHighWaterMark()` under representative preemption,
interrupt nesting and fault traffic.

| Task | Allocation | ARMCC5 max depth | Arithmetic margin | M3 assessment |
|---|---:|---:|---:|---|
| Protect | 160 words / 640 B | 272 B | 368 B | static margin present |
| Sample | 192 words / 768 B | 480 B | 288 B | static margin present |
| State | 384 words / 1,536 B | 1,104 B | 432 B | largest absolute depth |
| SOC | 192 words / 768 B | 536 B | 232 B | target watermark required |
| Balance | 256 words / 1,024 B | 840 B | 184 B | smallest static margin |
| CANTx | 240 words / 960 B | 752 B | 208 B | includes CAN and read-only UART service path in final rebuild |
| CANRx | 160 words / 640 B | 288 B | 352 B | target watermark required |
| Idle | 128 words / 512 B | not isolated | not claimed | kernel-created |
| Timer service | 160 words / 640 B | not isolated | not claimed | kernel-created; priority 2 |

Application task stacks total 6,336 B. Including Idle and Timer service stacks,
the configured task-stack payload is 7,488 B inside `ucHeap`.

## 3. RTOS objects and `heap_4` estimate

An ARMCC5 configuration-matched type probe performed for M3 measured:
`StaticTask_t=80 B`, `StaticQueue_t=72 B`, `StaticEventGroup_t=24 B`,
`BMS_CanFrame_t=20 B`, and `BMS_CcSample_t=12 B`. The temporary probe was not
added to the production project. Queue depths are TX 24, RX 12 and CC 8.

| Dynamic heap consumer | Payload/control estimate | `heap_4` allocation cost |
|---|---:|---:|
| 9 task stacks | 7,488 B | 7,560 B |
| 9 TCBs | 720 B | 792 B |
| 3 application queues, including storage | 1,032 B | 1,056 B |
| 2 mutexes + 1 binary semaphore | 216 B | 240 B |
| 1 event group | 24 B | 32 B |
| timer command queue, depth 8 | 168 B | 176 B |
| **Estimated allocated after scheduler startup** | **9,648 B payload/control** | **9,856 B** |

The right column applies the actual `heap_4` 8-byte block header/alignment rule.
Because the mapped `ucHeap` begins at `0x20000B84`, initialization alignment and
the end marker leave an estimated 12,272 B usable free block. The arithmetic
startup remainder is therefore approximately **2,416 B** (19.66% of the nominal
12 KiB arena).

This is a source/map-derived estimate. Fragmentation, future allocations and
real execution can only be closed with runtime evidence. The M3 UART `heap=`
fields expose current free/minimum-ever-free bytes through
`xPortGetFreeHeapSize()` and `xPortGetMinimumEverFreeHeapSize()` without adding
a command/control path.

## 4. Runtime closure criteria

Before hardware qualification, capture at least:

1. all seven task high-water marks after startup, normal charge/discharge,
   protection, recovery, CAN saturation and persistence activity;
2. current and minimum-ever-free FreeRTOS heap from the UART line or debugger;
3. stack overflow, malloc-failed and assert hooks remaining unentered;
4. longest observed interrupt nesting and task response latency;
5. repeated results on a release build with the same optimization settings.

No stack may be reduced from M3 static evidence alone. Any runtime high-water
value close to the end of a task allocation, any malloc failure, or an
unexplained decline in minimum-ever-free heap is a hardware-validation stop
condition. `REAL_HW validation has not been performed by this Milestone.`
