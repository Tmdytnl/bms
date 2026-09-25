#include "test_phase5.h"

volatile uint32_t g_phase5_test_failures;
volatile uint32_t g_phase5_test_completed;
volatile uint32_t g_p5_trip_failures;
volatile uint32_t g_p5_ocdscd_failures;
volatile uint32_t g_p5_fet_failures;
volatile uint32_t g_p5_cellbal_failures;

void SystemInit(void)
{
}

int main(void)
{
    g_p5_trip_failures = Test_Phase5_Trip();
    g_p5_ocdscd_failures = Test_Phase5_OcdScd();
    g_p5_fet_failures = Test_Phase5_Fet();
    g_p5_cellbal_failures = Test_Phase5_CellBal();
    g_phase5_test_failures = g_p5_trip_failures + g_p5_ocdscd_failures +
                             g_p5_fet_failures + g_p5_cellbal_failures;
    g_phase5_test_completed = 1UL;
    __breakpoint(0);

    while (1)
    {
    }
}
