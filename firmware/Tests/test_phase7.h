#ifndef TEST_PHASE7_H
#define TEST_PHASE7_H

#include <stdint.h>

#include "apl_rtos.h"
#include "bq76940.h"

extern volatile uint32_t g_p7_probe;

uint32_t Test_Phase7_ProtectLogic(void);
uint32_t Test_Phase7_CcQueue(void);
uint32_t Test_Phase7_AlertRetry(void);
uint32_t Test_Phase7_Xready(void);
uint32_t Test_Phase7_BoundaryContracts(void);
uint32_t Test_Phase7_SimCommPolicy(void);

/* ARMCC5 下执行 production queue/drain/service 的确定性 RTOS/BQ/BSP fake。 */
void TestP7_StubReset(void);
BQ76940_t *TestP7_Device(void);
void TestP7_SetMutexFailures(uint8_t failures);
void TestP7_SetAlertActive(bool active);
void TestP7_SetStatScript(const uint8_t *values,
                          const BQ76940_Status_t *statuses,
                          uint8_t count);
void TestP7_SetCcScript(const int16_t *values,
                        const BQ76940_Status_t *statuses,
                        uint8_t count);
void TestP7_SetWriteFailure(BQ76940_Status_t status, uint8_t failures);
void TestP7_SetReplacementFailures(uint8_t failures);
uint8_t TestP7_StatReadCount(void);
uint8_t TestP7_CcReadCount(void);
uint8_t TestP7_WriteCount(void);
uint8_t TestP7_WriteValue(uint8_t index);
uint8_t TestP7_QueueCount(void);
bool TestP7_QueuePop(BMS_CcSample_t *sample);
EventBits_t TestP7_EventBits(void);
bool TestP7_QueueOpsProtected(void);
bool TestP7_MutexAvailable(void);
uint32_t TestP7_SchedulerSuspendCount(void);
uint32_t TestP7_SchedulerResumeCount(void);
bool TestP7_SchedulerProtectionBalanced(void);
bool TestP7_RunProtectTaskRetryScenario(void);
bool TestP7_RunProtectTaskAlreadyHighScenario(void);
uint8_t TestP7_TaskDelayCount(void);
TickType_t TestP7_LastTaskDelay(void);
uint8_t TestP7_ExtiInitCount(void);
bool TestP7_ExerciseAlertIsr(void);

void TestP7_SetRecoveryResult(bool result);
bool TestP7_RecoveryHook(BQ76940_t *device);
uint8_t TestP7_RecoveryCallCount(void);

#endif /* TEST_PHASE7_H */
