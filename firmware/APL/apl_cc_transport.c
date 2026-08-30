#include "apl_rtos.h"

#include <stddef.h>

bool APL_Rtos_TransportCcSample(const BMS_CcSample_t *sample,
                                bool *overflowed,
                                bool *oldest_was_dropped)
{
    BMS_CcSample_t discarded;
    bool inserted;

    if ((sample == NULL) || (overflowed == NULL) ||
        (oldest_was_dropped == NULL) || (xCcSampleQueue == NULL))
    {
        return false;
    }
    *overflowed = false;
    *oldest_was_dropped = false;
    inserted = false;

    vTaskSuspendAll();
    if (xQueueSend(xCcSampleQueue, sample, 0U) == pdPASS)
    {
        inserted = true;
    }
    else
    {
        *overflowed = true;
        if (xQueueReceive(xCcSampleQueue, &discarded, 0U) == pdPASS)
        {
            *oldest_was_dropped = true;
            inserted = (xQueueSend(xCcSampleQueue, sample, 0U) == pdPASS);
        }
    }
    (void)xTaskResumeAll();

    if (*overflowed && (xSysEvents != NULL))
    {
        (void)xEventGroupSetBits(xSysEvents, EVT_CC_QUEUE_OVERFLOW);
    }
    return inserted;
}
