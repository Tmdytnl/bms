# BMS V1 Initial SRAM Budget

Status: Phase 1 baseline. This is not the final Phase 6 RTOS allocation.

Physical SRAM is fixed at 20 KiB (`0x20000000` + `0x5000`). The Phase 1
ARMCC5 map reports `RW-data=0` and `ZI-data=1856` bytes. That ZI total already
contains the 220-byte shared data snapshot, the startup file's 1024-byte MSP
stack and 512-byte C library heap, plus 96 bytes of C library workspace and
alignment. These items must not be added to the map total a second time.

| Budget item | Phase 1 evidence / initial target | Accounting rule |
|---|---:|---|
| RW-data | 0 B (current ARMCC5 map) | Included in future map total |
| ZI-data | 1856 B (current ARMCC5 map) | Includes the next three measured rows and shared data |
| MSP | 1024 B (current startup/map) | Already included in ZI-data |
| C library heap | 512 B (current startup/map) | Already included in ZI-data |
| BMS shared data | 220 B (`g_bms_data`, current map) | Already included in ZI-data |
| FreeRTOS heap_4 arena | About 8 KiB initial design target | Future `ucHeap`; will be included in ZI-data |
| Seven task stacks | Pending Phase 6 sizing/high-water evidence | Allocated from the FreeRTOS heap; do not add again beside `ucHeap` |
| Queues | Pending Phase 6 sizing | Allocated from the FreeRTOS heap unless made static |
| Mutexes/semaphores | Pending Phase 6 sizing | Allocated from the FreeRTOS heap unless made static |
| Event group | Pending Phase 6 sizing | Allocated from the FreeRTOS heap unless made static |
| CAN frame buffers | Pending protocol/RTOS design | Count in ZI or heap according to actual placement |
| BQ sample buffers | Pending driver/sample design | Count in ZI or heap according to actual placement |
| Safety margin | Pending complete map and stack high-water data | Must remain explicit before Phase 6 closes |

The 8 KiB FreeRTOS heap is an initial target only. Phase 6 must use the current
BMS map plus task stack high-water measurements to resize the heap, stacks and
RTOS objects while preserving an explicit safety margin.
