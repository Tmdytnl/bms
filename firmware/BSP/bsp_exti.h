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

/* EXTI1 逻辑优先级（C-02/Gate §3.3）。 */
#define BSP_EXTI1_LOGICAL_PRIORITY              (6U)

/*
 * 按冻结 priority 配置 PB1/EXTI1 rising edge；重复调用幂等重配同一 line。
 * 必须从任务上下文调用，成功后 EXTI1_IRQHandler 才拥有 give semaphore 权限。
 */
bool BSP_ALERT_EXTI_Init(void);

/* BSP_ALERT_EXTI_Init 成功后为 true。 */
bool BSP_ALERT_EXTI_IsInitialized(void);

/*
 * 直接读取 ALERT level，补偿 rising-edge 无法报告 startup-high 的情况。
 * true 只表示引脚当前为高，不代表某个 SYS_STAT 位身份，仍需 ProtectTask 读取。
 */
bool BSP_ALERT_PinActive(void);

#endif /* BSP_EXTI_H：头文件防重复包含 */
