#ifndef BSP_EXTI_H
#define BSP_EXTI_H

#include <stdbool.h>
#include <stdint.h>

/*
 * BQ ALERT EXTI1 BSP (Phase 7).
 *
 * PB1 / EXTI1 rising-edge ALERT input (spec §20.1, Gate §3.3):
 *   - logical preemption priority 6 (raw 0x60), below the FreeRTOS
 *     max-syscall priority 5 so FromISR calls are legal (C-02);
 *   - the ISR only clears the STM32 pending bit, gives xAfeAlertSem and
 *     yields; it never touches the BQ or I2C (H-05).
 *
 * NVIC grouping must be PriorityGroup_4 (set once before the scheduler
 * starts in Phase 6 main flow; this module asserts the grouping at init).
 */

/* EXTI1 logical priority (C-02/Gate §3.3). */
#define BSP_EXTI1_LOGICAL_PRIORITY              (6U)

/*
 * Configure PB1 as EXTI1 rising-edge interrupt with the locked logical
 * priority. Returns false if the configuration cannot be applied.
 * Idempotent: calling again reconfigures the same line.
 */
bool BSP_ALERT_EXTI_Init(void);

/* True after BSP_ALERT_EXTI_Init succeeded. */
bool BSP_ALERT_EXTI_IsInitialized(void);

/* Direct read of the ALERT pin (H-05: active polling fallback). */
bool BSP_ALERT_PinActive(void);

#endif /* BSP_EXTI_H */
