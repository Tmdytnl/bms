#include "apl_debug.h"


#include "fml_debug.h"
#include "os_api.h"
#include "bsp_uart.h"

#define APL_DEBUG_MAX_BYTES_PER_SERVICE          (8U)

/* 按发送预算从只读诊断缓冲区向 UART 非阻塞输出。 */
void APL_Debug_Service(uint32_t now_ms)
{
    /* 当前读写或换算的数值。 */
    uint8_t value;
    /* 当前诊断快照缓冲区的字节索引。 */
    uint8_t index;

    /* FML 负责稳定格式，APL 只补充 RTOS heap 指标并执行 bounded UART drain。 */
    (void)FML_Debug_PrepareSnapshot(now_ms,
                                    OS_HeapFreeBytes(),
                                    OS_HeapMinimumFreeBytes());
    for (index = 0U; index < APL_DEBUG_MAX_BYTES_PER_SERVICE; ++index)
    {
        if (!FML_Debug_PeekByte(&value) ||
            !BSP_UART1_TryWriteByte(value))
        {
            break;
        }
        FML_Debug_ConsumeByte();
    }
}
