#include "test_phase5.h"

#include <stdbool.h>

#include "bsp_bq76940_control.h"

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

    /* SYS_CTRL2 composition 只保留 CC_EN(bit6)。 */
    req.chg = BQ76940_FET_DESIRE_ENABLE;
    req.dsg = BQ76940_FET_DESIRE_ENABLE;
    ctrl2 = BSP_BQ76940_Control_SysCtrl2WithFets(0x40U, &req);
    TEST_CHECK(ctrl2 == 0x43U);    /* 保留 CC_EN，CHG/DSG on */

    req.chg = BQ76940_FET_DESIRE_DISABLE;
    req.dsg = BQ76940_FET_DESIRE_DISABLE;
    ctrl2 = BSP_BQ76940_Control_SysCtrl2WithFets(0x40U, &req);
    TEST_CHECK(ctrl2 == 0x40U);    /* 保留 CC_EN，双 FET off */

    /* factory DELAY_DIS、CC_ONESHOT 与 reserved bit 强制为低，不从 readback replay。 */
    req.chg = BQ76940_FET_DESIRE_DISABLE;
    req.dsg = BQ76940_FET_DESIRE_ENABLE;
    ctrl2 = BSP_BQ76940_Control_SysCtrl2WithFets(0xFFU, &req);
    TEST_CHECK(ctrl2 == 0x42U);

    /* DELAY_DIS(0x80) 不能从 factory/readback state 逸出。 */
    req.chg = BQ76940_FET_DESIRE_ENABLE;
    req.dsg = BQ76940_FET_DESIRE_DISABLE;
    ctrl2 = BSP_BQ76940_Control_SysCtrl2WithFets(0xC0U, &req);
    TEST_CHECK(ctrl2 == 0x41U);    /* CC_EN|CHG，清 DELAY_DIS */

    /* NULL request fail-safe：稳定 control 保留、双 FET off。 */
    ctrl2 = BSP_BQ76940_Control_SysCtrl2WithFets(0x43U, NULL);
    TEST_CHECK(ctrl2 == 0x40U);

    req.chg = BQ76940_FET_DESIRE_ENABLE;
    req.dsg = BQ76940_FET_DESIRE_DISABLE;
    ctrl2 = BSP_BQ76940_Control_SysCtrl2WithFets(0x3FU, &req);
    TEST_CHECK(ctrl2 == 0x01U);    /* command/reserved bit 全零 */

    /* readback decode。 */
    BSP_BQ76940_Control_ObserveFets(0x43U, &observed);
    TEST_CHECK(observed.chg_on && observed.dsg_on);
    BSP_BQ76940_Control_ObserveFets(0x40U, &observed);
    TEST_CHECK(!observed.chg_on && !observed.dsg_on);
    BSP_BQ76940_Control_ObserveFets(0x01U, &observed);
    TEST_CHECK(observed.chg_on && !observed.dsg_on);
    BSP_BQ76940_Control_ObserveFets(0x02U, &observed);
    TEST_CHECK(!observed.chg_on && observed.dsg_on);

    /* ApplyInhibits：fault inhibit 强制对应 FET off。 */
    req.chg = BQ76940_FET_DESIRE_ENABLE;
    req.dsg = BQ76940_FET_DESIRE_ENABLE;
    BSP_BQ76940_Control_ApplyInhibits(&req, true, false, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_ENABLE);

    BSP_BQ76940_Control_ApplyInhibits(&req, false, true, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_ENABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_DISABLE);

    BSP_BQ76940_Control_ApplyInhibits(&req, true, true, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_DISABLE);

    /* 无 inhibit 时 pass-through。 */
    BSP_BQ76940_Control_ApplyInhibits(&req, false, false, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_ENABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_ENABLE);

    effective.chg = BQ76940_FET_DESIRE_ENABLE;
    effective.dsg = BQ76940_FET_DESIRE_ENABLE;
    BSP_BQ76940_Control_ApplyInhibits(NULL, false, false, &effective);
    TEST_CHECK(effective.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(effective.dsg == BQ76940_FET_DESIRE_DISABLE);

    return failures;
}
