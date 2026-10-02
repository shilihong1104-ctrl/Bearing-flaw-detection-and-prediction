/**
 * control.c — 电机调速与转速测量
 *
 * 功能：
 *   - Control_SetSpeed(percent) : DAC1(PA4) 输出 0-3.3V 模拟调速信号
 *                                （当前保持 100% 全速，不干预生产）
 *   - Control_GetSpeedRPM()     : 获取电机转速（由 TIM3 输入捕获计算）
 */
#include "control.h"
#include "stm32u5xx_hal.h"

/* 外部 DAC 句柄 */
extern DAC_HandleTypeDef hdac1;

static float g_speed_rpm = 0;

void Control_SetSpeed(uint8_t percent)
{
    if (percent > 100) percent = 100;
    /* DAC 12-bit: 0-4095 对应 0-3.3V */
    uint32_t value = ((uint32_t)percent * 4095) / 100;
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, value);
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
}

void Control_UpdateSpeed(uint32_t period_ticks)
{
    /* TIM3 时钟 1MHz（PSC=159），period_ticks 为一个脉冲周期的 tick 数
       RPM = 60 * 1e6 / period_ticks / 每转脉冲数
       假设电机输出轴每转 1 个脉冲（接近开关） */
    if (period_ticks > 0) {
        g_speed_rpm = 60.0f * 1000000.0f / (float)period_ticks;
    }
}

float Control_GetSpeedRPM(void)
{
    return g_speed_rpm;
}
