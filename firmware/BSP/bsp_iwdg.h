#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * timeout_ms 用 nominal 40 kHz LSI 选择 prescaler/reload；开始后硬件不可停止。
 * 只有 health owner 在所有必需任务 generation 已推进后才可调用。
 */
bool BSP_IWDG_StartNominal(uint32_t timeout_ms);
/* 只执行 reload key；“当前是否健康”的判断不属于 BSP。 */
void BSP_IWDG_Feed(void);

#endif /* BSP_IWDG_H：头文件防重复包含 */
