#include "bsp_iwdg.h"

/*
 * BSP 只配置/刷新硬件 IWDG，不判断“是否应该喂狗”。StateTask 必须先由 health
 * generation 证明全部必需任务推进，再启动并成为唯一 BSP_IWDG_Feed 调用者。
 * 基本链路是独立 LSI 时钟 -> prescaler 分频 -> reload 倒计时 -> start；运行期
 * 每次 feed 重新装载计数。IWDG 独立于主时钟，启动后通常只能靠芯片复位停止，
 * 因此不能在系统健康证据尚未建立时提前启动或由多个任务随意刷新。
 */

#include "stm32f10x.h"

#define BSP_IWDG_NOMINAL_LSI_HZ                 (40000UL)
#define BSP_IWDG_PRESCALER                      (256UL)
#define BSP_IWDG_PRESCALER_CODE                 (6UL)
#define BSP_IWDG_RELOAD_MAX                     (0x0FFFUL)
#define BSP_IWDG_UPDATE_LIMIT                   (100000UL)

bool BSP_IWDG_StartNominal(uint32_t timeout_ms)
{
    uint32_t reload;
    uint32_t wait;

    if (timeout_ms == 0UL)
    {
        return false;
    }
    /* reload 寄存器存 N-1；先在 N 域验证 1..4096，避免减一后下溢。 */
    reload = ((BSP_IWDG_NOMINAL_LSI_HZ * timeout_ms) /
              (BSP_IWDG_PRESCALER * 1000UL));
    if ((reload == 0UL) || (reload > (BSP_IWDG_RELOAD_MAX + 1UL)))
    {
        return false;
    }
    --reload;
    /* 0x5555 开写权限；写 PR/RLR 后等待寄存器更新同步完成再启动。 */
    IWDG->KR = 0x5555U;
    IWDG->PR = BSP_IWDG_PRESCALER_CODE;
    IWDG->RLR = reload;
    wait = 0UL;
    while ((IWDG->SR != 0U) && (wait < BSP_IWDG_UPDATE_LIMIT))
    {
        ++wait;
    }
    if (IWDG->SR != 0U)
    {
        return false;
    }
    /* 先装载本次 reload，再用 0xCCCC 永久启动独立看门狗。 */
    IWDG->KR = 0xAAAAU;
    IWDG->KR = 0xCCCCU;
    return true;
}

void BSP_IWDG_Feed(void)
{
    /* 单一 magic write 刷新计数器；调用者必须已经完成跨任务健康判定。 */
    IWDG->KR = 0xAAAAU;
}
