#include "apl_system.h"

/* 建立 APL 系统并启动调度器；初始化失败时停留在安全空转。 */
int main(void)
{
    /* Reset 后只进入 APL composition；任一步失败都停在不可继续的安全空闲态。 */
    if (!APL_SystemInit())
    {
        APL_SafeIdle();
    }
    APL_SystemStart();
    APL_SafeIdle();
    return 0;
}
