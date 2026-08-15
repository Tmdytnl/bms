/*
 * BMS V1 FreeRTOS configuration.
 *
 * Created from scratch for the BMS project (Phase 6). The reference file
 * docs/FreeRTOS/FreeRTOSConfig.h is BLOCKED legacy input (max priorities
 * 5, 17 KiB heap, raw 0xBF, diagnostics off) and must never be copied.
 *
 * Locked baseline (errata C-01/C-02, H-08/H-09; Software Gate §3.2):
 *   - configMAX_PRIORITIES 8; seven task priorities 5/4/3/3/2/2/2
 *   - 4-bit NVIC, PriorityGroup_4, kernel raw 0xF0, max syscall raw 0x50
 *   - initial heap target about 8 KiB (H-08: budget, not immutable)
 *   - configASSERT + stack overflow check 2 + hooks (H-09)
 *   - preemptive, 1 ms tick, native FreeRTOS API
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#include "bms_build_assert.h"

/* ------------------------------------------------------------------ */
/* Kernel basics                                                       */
/* ------------------------------------------------------------------ */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (72000000UL)
#define configTICK_RATE_HZ                      (1000UL)
#define configMAX_PRIORITIES                    (8)
#define configMINIMAL_STACK_SIZE                (128)
#define configMAX_TASK_NAME_LEN                 (12)
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

/* ------------------------------------------------------------------ */
/* Memory (H-08: 8 KiB is the initial budget, not a permanent value)   */
/* ------------------------------------------------------------------ */
#define configTOTAL_HEAP_SIZE                   (8 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0

/* ------------------------------------------------------------------ */
/* Timers (V11.1.0 requires configUSE_TIMERS to use timers.c)          */
/* ------------------------------------------------------------------ */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (2)
#define configTIMER_QUEUE_LENGTH                (8)
#define configTIMER_TASK_STACK_DEPTH            (160)

/* ------------------------------------------------------------------ */
/* Hooks and diagnostics (H-09)                                        */
/* ------------------------------------------------------------------ */
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configUSE_APPLICATION_TASK_TAG          0

/* V11 API availability. */
#define INCLUDE_xTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_uxTaskGetStackHighWaterMark      1

/* ------------------------------------------------------------------ */
/* Cortex-M3 / ARMCC5 port (C-02)                                      */
/* ------------------------------------------------------------------ */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

/* Map the FreeRTOS port exception handlers onto the startup vector
 * names used by startup_stm32f10x_md.s (RVDS/ARM_CM3 convention). */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

/* ------------------------------------------------------------------ */
/* Assert (H-09): custom handler must be provided by the application.  */
/* ------------------------------------------------------------------ */
void vApplicationAssertFailedHandler(void);
#define configASSERT(x)                         \
    do                                         \
    {                                          \
        if ((x) == 0)                          \
        {                                      \
            taskDISABLE_INTERRUPTS();          \
            vApplicationAssertFailedHandler(); \
        }                                      \
    } while (0)

/* ------------------------------------------------------------------ */
/* Hooks: only the assert handler is declared here (the kernel has no  */
/* prototype for it). The stack-overflow hook signature is provided by */
/* task.h; the implementations live in App/app_rtos_hooks.c.           */
/* ------------------------------------------------------------------ */
void vApplicationAssertFailedHandler(void);
void vApplicationMallocFailedHook(void);
void vApplicationIdleHook(void);

/* ------------------------------------------------------------------ */
/* Compile-time consistency checks (fail fast at build time).          */
/* ------------------------------------------------------------------ */
BMS_BUILD_ASSERT(configMAX_PRIORITIES == 8,
                 freertos_max_priorities_is_eight);
BMS_BUILD_ASSERT(configTICK_RATE_HZ == 1000,
                 freertos_tick_is_one_khz);
BMS_BUILD_ASSERT(configPRIO_BITS == 4,
                 freertos_prio_bits_is_four);
BMS_BUILD_ASSERT(configKERNEL_INTERRUPT_PRIORITY == 0xF0,
                 kernel_raw_priority_is_f0);
BMS_BUILD_ASSERT(configMAX_SYSCALL_INTERRUPT_PRIORITY == 0x50,
                 max_syscall_raw_priority_is_50);
BMS_BUILD_ASSERT(configUSE_PORT_OPTIMISED_TASK_SELECTION == 1,
                 port_optimised_task_selection_enabled);
BMS_BUILD_ASSERT(configCHECK_FOR_STACK_OVERFLOW == 2,
                 stack_overflow_check_is_two);
BMS_BUILD_ASSERT(configTOTAL_HEAP_SIZE <= (20 * 1024),
                 heap_budget_inside_sram);

#endif /* FREERTOS_CONFIG_H */
