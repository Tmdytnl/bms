#ifndef APL_SYSTEM_H
#define APL_SYSTEM_H

#include <stdbool.h>

/* 按 fail-safe 顺序组装所有必需模块；false 表示禁止启动调度器。 */
bool APL_SystemInit(void);
/* 仅在初始化成功后启动 FreeRTOS，正常情况不返回。 */
void APL_SystemStart(void);
/* 初始化或调度器失败时关闭中断并永久停在安全空转。 */
void APL_SafeIdle(void);

#endif /* APL_SYSTEM_H */
