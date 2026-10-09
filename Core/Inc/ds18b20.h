#ifndef __DS18B20_H
#define __DS18B20_H

#include "main.h"
#include <stdint.h>

/**
 * @brief  初始化 DS18B20（发送复位脉冲，检测存在脉冲）
 * @retval 0=检测到 DS18B20，-1=无响应
 */
int8_t DS18B20_Init(void);

/**
 * @brief  读取温度（单位：摄氏度 ×10，如 256 表示 25.6℃）
 *         阻塞式：内部等待 750ms 转换完成
 * @retval 温度值 ×10，读取失败返回 -999
 */
int16_t DS18B20_ReadTempX10(void);

/**
 * @brief  启动温度转换（非阻塞，立即返回）
 *         调用后需等待 ≥750ms 再调用 DS18B20_ReadResultX10()
 * @retval 0=成功，-1=总线无响应
 */
int8_t DS18B20_StartConversion(void);

/**
 * @brief  读取上一次启动转换的结果（非阻塞）
 *         必须在 DS18B20_StartConversion() 之后 ≥750ms 调用
 * @retval 温度值 ×10，读取失败返回 -999
 */
int16_t DS18B20_ReadResultX10(void);

#endif /* __DS18B20_H */
