#include "test_phase7.h"
#include "test_phase5.h"

volatile uint32_t g_phase7_test_failures;
volatile uint32_t g_phase7_test_completed;
volatile uint32_t g_p7_protect_failures;
volatile uint32_t g_p7_cc_failures;
volatile uint32_t g_p7_retry_failures;
volatile uint32_t g_p7_xready_failures;
volatile uint32_t g_p7_boundary_failures;
volatile uint32_t g_p7_sim_comm_failures;
volatile uint32_t g_p7_probe;
volatile uint32_t g_p5_trip_failures;
volatile uint32_t g_p5_ocdscd_failures;
volatile uint32_t g_p5_fet_failures;
volatile uint32_t g_p5_cellbal_failures;

void SystemInit(void)
{
}

int main(void)
{
    g_p7_probe = 1UL;
    g_p5_trip_failures = Test_Phase5_Trip();
    g_p5_ocdscd_failures = Test_Phase5_OcdScd();
    g_p5_fet_failures = Test_Phase5_Fet();
    g_p5_cellbal_failures = Test_Phase5_CellBal();
    g_p7_probe = 2UL;
    g_p7_protect_failures = Test_Phase7_ProtectLogic();
    g_p7_probe = 3UL;
    g_p7_cc_failures = Test_Phase7_CcQueue();
    g_p7_probe = 4UL;
    g_p7_retry_failures = Test_Phase7_AlertRetry();
    g_p7_probe = 5UL;
    g_p7_xready_failures = Test_Phase7_Xready();
    g_p7_probe = 6UL;
    g_p7_boundary_failures = Test_Phase7_BoundaryContracts();
    g_p7_sim_comm_failures = Test_Phase7_SimCommPolicy();
    g_p7_probe = 7UL;
    g_phase7_test_failures =
        g_p5_trip_failures + g_p5_ocdscd_failures +
        g_p5_fet_failures + g_p5_cellbal_failures +
        g_p7_protect_failures + g_p7_cc_failures +
        g_p7_retry_failures + g_p7_xready_failures +
        g_p7_boundary_failures + g_p7_sim_comm_failures;
    g_phase7_test_completed = 1UL;
    g_p7_probe = 8UL;
    __breakpoint(0);

    while (1)
    {
    }
}
