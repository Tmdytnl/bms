#ifndef TEST_PHASE8_DATA_HOST_SHIM_H
#define TEST_PHASE8_DATA_HOST_SHIM_H

/*
 * host-only ABI shim，用于执行未改动的 production bms_data.c；ARMCC5 target test
 * 使用真实 FreeRTOS header。
 */
#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdint.h>

typedef int32_t BaseType_t;
typedef uint32_t TickType_t;
struct QueueDefinition;
typedef struct QueueDefinition *QueueHandle_t;
typedef QueueHandle_t SemaphoreHandle_t;

#define pdFALSE                         ((BaseType_t)0)
#define pdTRUE                          ((BaseType_t)1)
#define pdPASS                          pdTRUE
#define pdFAIL                          pdFALSE
#define queueSEND_TO_BACK               ((BaseType_t)0)
#define semGIVE_BLOCK_TIME              ((TickType_t)0U)

extern SemaphoreHandle_t xDataMutex;

BaseType_t xQueueSemaphoreTake(QueueHandle_t queue,
                               TickType_t ticks_to_wait);
BaseType_t xQueueGenericSend(QueueHandle_t queue,
                             const void *item,
                             TickType_t ticks_to_wait,
                             BaseType_t copy_position);

#define xSemaphoreTake(semaphore, ticks) \
    xQueueSemaphoreTake((semaphore), (ticks))
#define xSemaphoreGive(semaphore) \
    xQueueGenericSend((QueueHandle_t)(semaphore), NULL, \
                      semGIVE_BLOCK_TIME, queueSEND_TO_BACK)

#endif /* APP_RTOS_H */
#endif /* TEST_PHASE8_DATA_HOST_SHIM_H */
