#include "app_rtos.h"

/* Phase 6 verifies the seven-task creation contract before the scheduler
 * starts. Phase 7 executes the real ProtectTask in its own regression image;
 * this non-running entry keeps the Phase 6 foundation image scoped to RTOS
 * object/task construction. */
void Task_Protect(void *argument)
{
    (void)argument;
    for (;;)
    {
    }
}

/* Phase 8 executes the production SampleTask in its own image. Keep this
 * Phase 6 construction-only image independent of measurement drivers. */
void Task_Sample(void *argument)
{
    (void)argument;
    for (;;)
    {
    }
}
