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
 * @retval 温度值 ×10，读取失败返回 -999
 */
int16_t DS18B20_ReadTempX10(void);

#endif /* __DS18B20_H */
