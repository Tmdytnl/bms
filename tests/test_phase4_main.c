#include "test_phase4.h"

volatile uint32_t g_phase4_test_failures;
volatile uint32_t g_phase4_test_completed;
volatile uint32_t g_phase4_mapping_failures;
volatile uint32_t g_phase4_measurement_failures;

void SystemInit(void)
{
}

int main(void)
{
    g_phase4_mapping_failures = Test_Phase4_Mapping();
    g_phase4_measurement_failures = Test_Phase4_Measurement();
    g_phase4_test_failures = g_phase4_mapping_failures +
                             g_phase4_measurement_failures;
    g_phase4_test_completed = 1UL;
    __breakpoint(0);

    while (1)
    {
    }
}
