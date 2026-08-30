#include "apl_system.h"

int main(void)
{
    if (!APL_SystemInit())
    {
        APL_SafeIdle();
    }
    APL_SystemStart();
    APL_SafeIdle();
    return 0;
}
