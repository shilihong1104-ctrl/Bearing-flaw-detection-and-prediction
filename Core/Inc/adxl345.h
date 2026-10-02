#ifndef __ADXL345_H
#define __ADXL345_H

#include "main.h"
#include <stdint.h>

/* ADXL345 寄存器地址 */
#define ADXL345_REG_DEVID       0x00    /* 设备 ID，固定值 0xE5 */
#define ADXL345_REG_POWER_CTL   0x2D    /* 电源控制 */
#define ADXL345_REG_DATA_FORMAT 0x31    /* 数据格式 */
#define ADXL345_REG_BW_RATE     0x2C    /* 带宽/输出速率 */
#define ADXL345_REG_DATAX0      0x32    /* X 轴数据低字节 */

/* DEVID 期望值 */
#define ADXL345_DEVID_VALUE     0xE5

/* 量程选择（DATA_FORMAT 寄存器 bit0-1） */
typedef enum {
    ADXL345_RANGE_2G  = 0x00,
    ADXL345_RANGE_4G  = 0x01,
    ADXL345_RANGE_8G  = 0x02,
    ADXL345_RANGE_16G = 0x03
} ADXL345_Range_t;

/* 输出数据速率（BW_RATE 寄存器 bit0-3） */
typedef enum {
    ADXL345_RATE_3200HZ = 0x0F,
    ADXL345_RATE_1600HZ = 0x0E,
    ADXL345_RATE_800HZ  = 0x0D,
    ADXL345_RATE_400HZ  = 0x0C,
    ADXL345_RATE_200HZ  = 0x0B,
    ADXL345_RATE_100HZ  = 0x0A
} ADXL345_Rate_t;

/* 三轴原始数据（16 位有符号） */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} ADXL345_Data_t;

/**
 * @brief  初始化 ADXL345：检查 DEVID、配置量程和速率、进入测量模式
 * @retval 0=成功，-1=DEVID 不匹配
 */
int8_t ADXL345_Init(ADXL345_Range_t range, ADXL345_Rate_t rate);

/**
 * @brief  读取 DEVID 寄存器（用于验证通信）
 * @retval 设备 ID（正常应为 0xE5）
 */
uint8_t ADXL345_ReadDevID(void);

/**
 * @brief  读取三轴加速度原始值
 * @param  data  输出结构体
 */
void ADXL345_ReadAccel(ADXL345_Data_t *data);

#endif /* __ADXL345_H */
