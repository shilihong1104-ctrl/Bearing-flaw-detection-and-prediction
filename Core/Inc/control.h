#ifndef __CONTROL_H
#define __CONTROL_H

#include "main.h"
#include <stdint.h>

/**
 * @brief  设置电机目标转速（百分比 0-100）
 *         通过 DAC 输出 0-3.3V 给调速器
 * @param  percent  0-100
 */
void Control_SetSpeed(uint8_t percent);

/**
 * @brief  获取当前电机转速（RPM），由 TIM3 输入捕获更新
 * @retval 转速 RPM
 */
float Control_GetSpeedRPM(void);

/**
 * @brief  更新转速测量（在 TIM3 捕获回调中调用）
 */
void Control_UpdateSpeed(uint32_t period_ticks);

#endif /* __CONTROL_H */
