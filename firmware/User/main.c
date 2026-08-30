#include "apl_system.h"

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
