#ifndef BSP_EXTI_H
#define BSP_EXTI_H

#include <stdbool.h>
#include <stdint.h>

/*
 * BQ ALERT 使用 PB1/EXTI1 rising edge。logical priority 6（raw 0x60）不高于
 * 项目定义的 syscall-capable IRQ 上限。BSP 只配置/查询/清除 EXTI peripheral；
 * semaphore/yield handoff 留在 APL ISR，BQ/I2C 事务留在任务上下文。
 * NVIC PriorityGroup_4 由 APL composition 在 scheduler 前统一配置。
 */

/* EXTI1 逻辑优先级（C-02/Gate §3.3）。 */
#define BSP_EXTI1_LOGICAL_PRIORITY              (6U)

/*
 * 按冻结 priority 配置 PB1/EXTI1 rising edge；重复调用幂等重配同一 line。
 * 上层必须在 IRQ handoff 环境就绪后调用，避免启动早期边沿进入未就绪的软件路径。
 */
bool BSP_ALERT_EXTI_Init(void);

/* BSP_ALERT_EXTI_Init 成功后为 true。 */
bool BSP_ALERT_EXTI_IsInitialized(void);

/*
 * 直接读取 ALERT level，补偿 rising-edge 无法报告 startup-high 的情况。
 * true 只表示引脚当前为高，不代表某个 SYS_STAT 位身份，仍需 ProtectTask 读取。
 */
bool BSP_ALERT_PinActive(void);

/* RTOS-aware ISR handoff 留在 APL；BSP 只暴露 peripheral primitive。 */
bool BSP_ALERT_EXTI_IsPending(void);
void BSP_ALERT_EXTI_ClearPending(void);

#endif /* BSP_EXTI_H：头文件防重复包含 */
