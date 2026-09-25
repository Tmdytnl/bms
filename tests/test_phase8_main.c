#include <stdint.h>

#if defined(TEST_PHASE8_DATA_IMAGE)
#include "test_phase8_data.h"
#elif defined(TEST_PHASE8_SAMPLE_IMAGE)
#include "test_phase8_sample.h"
#elif defined(TEST_PHASE8_AFE_IMAGE)
uint32_t Test_Phase8_AfeStartup(void);
#else
#error One Phase 8 test-image selector must be defined
#endif

volatile uint32_t g_phase8_test_suite_id;
volatile uint32_t g_phase8_test_failures;
volatile uint32_t g_phase8_test_completed;
volatile uint32_t g_phase8_test_probe;

void SystemInit(void)
{
}

int main(void)
{
    g_phase8_test_probe = 1UL;

#if defined(TEST_PHASE8_DATA_IMAGE)
    g_phase8_test_suite_id = 1UL;
    g_phase8_test_failures = Test_Phase8_Data();
#elif defined(TEST_PHASE8_SAMPLE_IMAGE)
    g_phase8_test_suite_id = 2UL;
    g_phase8_test_failures = Test_Phase8_Sample();
#elif defined(TEST_PHASE8_AFE_IMAGE)
    g_phase8_test_suite_id = 3UL;
    g_phase8_test_failures = Test_Phase8_AfeStartup();
#endif

    g_phase8_test_probe = 2UL;
    g_phase8_test_completed = 1UL;
    g_phase8_test_probe = 3UL;
    __breakpoint(0);

    while (1)
    {
    }
}
