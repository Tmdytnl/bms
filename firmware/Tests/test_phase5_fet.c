#include "test_phase5.h"

#include <stdbool.h>

#include "bq76940_control.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

uint32_t Test_Phase5_Fet(void)
{
    uint32_t failures;
    BQ76940_FetRequest_t req;
    BQ76940_FetRequest_t effective;
    BQ76940_FetObserved_t observed;
    uint8_t ctrl2;

    failures = 0UL;

    /* SYS_CTRL2 with FETs: preserve CC_EN (bit 6) and DELAY_DIS (bit 7). */
    req.chg = BQ76940_FET_DESIRE_ENABLE;
    req.dsg = BQ76940_FET_DESIRE_ENABLE;
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0x40U, &req);
    TEST_CHECK(ctrl2 == 0x43U);    /* CC_EN kept, CHG+DSG on */

    req.chg = BQ76940_FET_DESIRE_DISABLE;
    req.dsg = BQ76940_FET_DESIRE_DISABLE;
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0x40U, &req);
    TEST_CHECK(ctrl2 == 0x40U);    /* CC_EN kept, both FETs off */

    /* Only CHG cleared; DSG stays on; high bits preserved. */
    req.chg = BQ76940_FET_DESIRE_DISABLE;
    req.dsg = BQ76940_FET_DESIRE_ENABLE;
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xFFU, &req);
    TEST_CHECK(ctrl2 == 0xFEU);

    /* DELAY_DIS (0x80) preserved when toggling. */
    req.chg = BQ76940_FET_DESIRE_ENABLE;
    req.dsg = BQ76940_FET_DESIRE_DISABLE;
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xC0U, &req);
    TEST_CHECK(ctrl2 == 0xC1U);    /* 0x80 | 0x40 | CHG */

    /* NULL request returns current unchanged. */
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0x43U, NULL);
    TEST_CHECK(ctrl2 == 0x43U);

    /* Observe. */
    BQ76940_Control_ObserveFets(0x43U, &observed);
    TEST_CHECK(observed.chg_on && observed.dsg_on);
    BQ76940_Control_ObserveFets(0x40U, &observed);
    TEST_CHECK(!observed.chg_on && !observed.dsg_on);
    BQ76940_Control_ObserveFets(0x01U, &observed);
    TEST_CHECK(observed.chg_on && !observed.dsg_on);
    BQ76940_Control_ObserveFets(0x02U, &observed);
    TEST_CHECK(!observed.chg_on && observed.dsg_on);

    /* ApplyInhibits: fault inhibit forces FET off. */
    req.chg = BQ76940_FET_DESIRE_ENABLE;
    req.dsg = BQ76940_FET_DESIRE_ENABLE;
    BQ76940_Control_ApplyInhibits(&req, true, false, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_ENABLE);

    BQ76940_Control_ApplyInhibits(&req, false, true, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_ENABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_DISABLE);

    BQ76940_Control_ApplyInhibits(&req, true, true, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_DISABLE);

    /* No inhibits: pass-through. */
    BQ76940_Control_ApplyInhibits(&req, false, false, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_ENABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_ENABLE);

    return failures;
}
