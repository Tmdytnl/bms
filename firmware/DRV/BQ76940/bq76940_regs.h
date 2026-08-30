#ifndef BQ76940_REGS_H
#define BQ76940_REGS_H

#include <stdint.h>

#include "bq76940_build_assert.h"

/* BQ7694003：启用 CRC、3.3 V REGOUT、7 位地址 0x08。 */
#define BQ76940_I2C_ADDRESS_7BIT          (0x08U)
#define BQ76940_I2C_WIRE_WRITE           (0x10U)
#define BQ76940_I2C_WIRE_READ            (0x11U)

/* 0x00..0x0B：状态、输出控制与硬件保护配置；SYS_STAT 含 W1C 位。 */
#define BQ76940_REG_SYS_STAT              (0x00U)
#define BQ76940_REG_CELLBAL1              (0x01U)
#define BQ76940_REG_CELLBAL2              (0x02U)
#define BQ76940_REG_CELLBAL3              (0x03U)
#define BQ76940_REG_SYS_CTRL1             (0x04U)
#define BQ76940_REG_SYS_CTRL2             (0x05U)
#define BQ76940_REG_PROTECT1              (0x06U)
#define BQ76940_REG_PROTECT2              (0x07U)
#define BQ76940_REG_PROTECT3              (0x08U)
#define BQ76940_REG_OV_TRIP               (0x09U)
#define BQ76940_REG_UV_TRIP               (0x0AU)
#define BQ76940_REG_CC_CFG                (0x0BU)
#define BQ76940_CC_CFG_REQUIRED_VALUE     (0x19U)

/* SYS_CTRL2：FET compositor 只改 CHG/DSG，调度期 transaction 必须显式保留 CC_EN。 */
#define BQ76940_SYS_CTRL2_CC_EN_MASK       ((uint8_t)0x40U)
#define BQ76940_SYS_CTRL2_DSG_ON_MASK      ((uint8_t)0x02U)
#define BQ76940_SYS_CTRL2_CHG_ON_MASK      ((uint8_t)0x01U)
#define BQ76940_SYS_CTRL2_FET_MASK         \
    ((uint8_t)(BQ76940_SYS_CTRL2_DSG_ON_MASK | \
               BQ76940_SYS_CTRL2_CHG_ON_MASK))

/* 0x0C..0x29：15 个物理 VC channel；13S 板跳过短接的 VC9 与 VC14。 */
#define BQ76940_REG_VC1_HI                (0x0CU)
#define BQ76940_REG_VC1_LO                (0x0DU)
#define BQ76940_REG_VC2_HI                (0x0EU)
#define BQ76940_REG_VC2_LO                (0x0FU)
#define BQ76940_REG_VC3_HI                (0x10U)
#define BQ76940_REG_VC3_LO                (0x11U)
#define BQ76940_REG_VC4_HI                (0x12U)
#define BQ76940_REG_VC4_LO                (0x13U)
#define BQ76940_REG_VC5_HI                (0x14U)
#define BQ76940_REG_VC5_LO                (0x15U)
#define BQ76940_REG_VC6_HI                (0x16U)
#define BQ76940_REG_VC6_LO                (0x17U)
#define BQ76940_REG_VC7_HI                (0x18U)
#define BQ76940_REG_VC7_LO                (0x19U)
#define BQ76940_REG_VC8_HI                (0x1AU)
#define BQ76940_REG_VC8_LO                (0x1BU)
#define BQ76940_REG_VC9_HI                (0x1CU)
#define BQ76940_REG_VC9_LO                (0x1DU)
#define BQ76940_REG_VC10_HI               (0x1EU)
#define BQ76940_REG_VC10_LO               (0x1FU)
#define BQ76940_REG_VC11_HI               (0x20U)
#define BQ76940_REG_VC11_LO               (0x21U)
#define BQ76940_REG_VC12_HI               (0x22U)
#define BQ76940_REG_VC12_LO               (0x23U)
#define BQ76940_REG_VC13_HI               (0x24U)
#define BQ76940_REG_VC13_LO               (0x25U)
#define BQ76940_REG_VC14_HI               (0x26U)
#define BQ76940_REG_VC14_LO               (0x27U)
#define BQ76940_REG_VC15_HI               (0x28U)
#define BQ76940_REG_VC15_LO               (0x29U)

/* BAT/TS/CC 都是相邻 HI/LO 对，应通过一次 block transaction 原子读取。 */
#define BQ76940_REG_BAT_HI                (0x2AU)
#define BQ76940_REG_BAT_LO                (0x2BU)
#define BQ76940_REG_TS1_HI                (0x2CU)
#define BQ76940_REG_TS1_LO                (0x2DU)
#define BQ76940_REG_TS2_HI                (0x2EU)
#define BQ76940_REG_TS2_LO                (0x2FU)
#define BQ76940_REG_TS3_HI                (0x30U)
#define BQ76940_REG_TS3_LO                (0x31U)
#define BQ76940_REG_CC_HI                 (0x32U)
#define BQ76940_REG_CC_LO                 (0x33U)

/* 出厂校准值分散在三个寄存器；gain trim 需要由 ADCGAIN1/2 拼接。 */
#define BQ76940_REG_ADCGAIN1              (0x50U)
#define BQ76940_REG_ADCOFFSET             (0x51U)
#define BQ76940_REG_ADCGAIN2              (0x59U)

#define BQ76940_ADCGAIN1_MASK             (0x0CU)
#define BQ76940_ADCGAIN2_MASK             (0xE0U)
#define BQ76940_ADC_GAIN_BASE_UV_PER_LSB  (365U)
#define BQ76940_ADC_GAIN_MAX_UV_PER_LSB   (396U)
#define BQ76940_CELL_RAW14_MASK           (0x3FFFU)

BQ76940_BUILD_ASSERT(BQ76940_I2C_WIRE_WRITE ==
                     (BQ76940_I2C_ADDRESS_7BIT << 1),
                 bq_wire_write_matches_seven_bit_address);
BQ76940_BUILD_ASSERT(BQ76940_I2C_WIRE_READ ==
                     ((BQ76940_I2C_ADDRESS_7BIT << 1) | 1U),
                 bq_wire_read_matches_seven_bit_address);
BQ76940_BUILD_ASSERT(BQ76940_REG_VC15_LO == 0x29U,
                 bq_cell_register_range_matches_datasheet);
BQ76940_BUILD_ASSERT(BQ76940_REG_CC_LO == (BQ76940_REG_CC_HI + 1U),
                 bq_cc_registers_are_adjacent);
BQ76940_BUILD_ASSERT(BQ76940_ADC_GAIN_MAX_UV_PER_LSB ==
                     (BQ76940_ADC_GAIN_BASE_UV_PER_LSB + 31U),
                 bq_adc_gain_trim_range_is_thirty_two_values);
/* 上述断言把芯片型号/地址/寄存器窗口假设变成编译期契约，防止静默漂移。 */

#endif /* BQ76940_REGS_H：头文件防重复包含 */
