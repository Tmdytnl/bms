#include "apl_debug.h"

#include "FreeRTOS.h"

#include "bms_debug.h"
#include "bsp_uart.h"

#define APL_DEBUG_MAX_BYTES_PER_SERVICE          (8U)

/* 按发送预算从只读诊断缓冲区向 UART 非阻塞输出。 */
void APL_Debug_Service(uint32_t now_ms)
{
    uint8_t value;
    uint8_t index;

    /* FML 负责稳定格式，APL 只补充 RTOS heap 指标并执行 bounded UART drain。 */
    (void)BMS_Debug_PrepareSnapshot(now_ms,
                                    xPortGetFreeHeapSize(),
                                    xPortGetMinimumEverFreeHeapSize());
    for (index = 0U; index < APL_DEBUG_MAX_BYTES_PER_SERVICE; ++index)
    {
        if (!BMS_Debug_PeekByte(&value) ||
            !BSP_UART1_TryWriteByte(value))
        {
            break;
        }
        BMS_Debug_ConsumeByte();
    }
}
