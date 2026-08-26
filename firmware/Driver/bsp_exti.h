#ifndef BSP_EXTI_H
#define BSP_EXTI_H

#include <stdbool.h>
#include <stdint.h>

/*
 * BQ ALERT 使用 PB1/EXTI1 rising edge。logical priority 6（raw 0x60）不高于
 * FreeRTOS max-syscall priority 5，因此 ISR 可以合法调用 FromISR API。
 * ISR 只清 STM32 pending、give xAfeAlertSem、按需 yield，不访问 BQ/I2C。
 * NVIC PriorityGroup_4 在 scheduler 前一次配置；EXTI 由 ProtectTask 启用，
 * 确保 port priority validator 已初始化。
 */

/* EXTI1 logical priority（C-02/Gate §3.3）。 */
#define BSP_EXTI1_LOGICAL_PRIORITY              (6U)

/* 按冻结 priority 配置 PB1/EXTI1 rising edge；重复调用幂等重配同一 line。 */
bool BSP_ALERT_EXTI_Init(void);

/* BSP_ALERT_EXTI_Init 成功后为 true。 */
bool BSP_ALERT_EXTI_IsInitialized(void);

/* 直接读取 ALERT level，补偿 rising-edge 无法报告 startup-high 的情况。 */
bool BSP_ALERT_PinActive(void);

#endif /* BSP_EXTI_H：include guard */
