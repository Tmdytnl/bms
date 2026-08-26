#include "test_phase6.h"

volatile uint32_t g_phase6_test_failures;
volatile uint32_t g_phase6_test_completed;
volatile uint32_t g_p6_objects_failures;
volatile uint32_t g_p6_tasks_failures;
volatile uint32_t g_p6_probe;   /* Simulator 诊断用 progress probe */

void SystemInit(void)
{
}

int main(void)
{
    g_p6_probe = 1UL;
    g_p6_objects_failures = Test_Phase6_Objects();
    g_p6_probe = 2UL;
    g_p6_tasks_failures = Test_Phase6_Tasks();
    g_p6_probe = 3UL;
    g_phase6_test_failures = g_p6_objects_failures + g_p6_tasks_failures;
    g_phase6_test_completed = 1UL;
    g_p6_probe = 4UL;
    __breakpoint(0);

    while (1)
    {
    }
}
