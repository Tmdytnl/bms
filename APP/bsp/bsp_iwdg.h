#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * timeout_ms 用 nominal 40 kHz LSI 选择 prescaler/reload；开始后硬件不可停止。
 * 调用者负责决定启动时机；false 表示参数越界或寄存器更新未在有界等待内完成。
 */
bool BSP_IWDG_StartNominal(uint32_t timeout_ms);
/* 只执行 reload key；是否允许刷新不属于 BSP。 */
void BSP_IWDG_Feed(void);

#endif /* BSP_IWDG_H：头文件防重复包含 */
