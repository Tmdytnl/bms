#include "bsp_iwdg.h"

/*
 * BSP 只配置/刷新硬件 IWDG，不判断“是否应该喂狗”。基本链路是独立 LSI 时钟
 * -> prescaler 分频 -> reload 倒计时 -> start；每次 feed 只重新装载计数。
 * IWDG 独立于主时钟，启动后通常只能靠芯片复位停止，因此调用时机与唯一调用者
 * 必须由上层 composition/health policy 约束，BSP 不含业务健康语义。
 */

#include "stm32f10x.h"

#define BSP_IWDG_NOMINAL_LSI_HZ                 (40000UL)
#define BSP_IWDG_PRESCALER                      (256UL)
#define BSP_IWDG_PRESCALER_CODE                 (6UL)
#define BSP_IWDG_RELOAD_MAX                     (0x0FFFUL)
#define BSP_IWDG_UPDATE_LIMIT                   (100000UL)

/* 按标称超时配置并启动 IWDG，返回硬件启动是否成功。 */
bool BSP_IWDG_StartNominal(uint32_t timeout_ms)
{
    /* 依据超时配置换算的 IWDG 重装计数。 */
    uint32_t reload;
    /* 等待 IWDG 状态更新的有界循环计数。 */
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

/* 刷新已启动的 IWDG 硬件计数器，不作上层健康判断。 */
void BSP_IWDG_Feed(void)
{
    /* 单一 magic write 刷新计数器；BSP 不判断本次刷新是否被上层授权。 */
    IWDG->KR = 0xAAAAU;
}
