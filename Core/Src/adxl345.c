/**
 * adxl345.c — ADXL345 三轴加速度计驱动
 *
 * 通信协议：SPI Mode 3（CPOL=1, CPHA=1）
 * 接线：CS=PC3, SCK=PB10, MOSI=PC1, MISO=PC2
 * 功能：
 *   - ADXL345_Init()      : 配置量程和采样率，启动测量
 *   - ADXL345_ReadDevID() : 读设备 ID（应为 0xE5，用于验证通信）
 *   - ADXL345_ReadAccel() : 读取 X/Y/Z 三轴加速度（int16）
 */
#include "adxl345.h"
#include "spi.h"

/* 外部 SPI 句柄（由 spi.c 定义） */
extern SPI_HandleTypeDef hspi2;

/* CS 引脚定义（来自 main.h） */
#define ADXL345_CS_LOW()   HAL_GPIO_WritePin(ADXL345_CS_GPIO_Port, ADXL345_CS_Pin, GPIO_PIN_RESET)
#define ADXL345_CS_HIGH()  HAL_GPIO_WritePin(ADXL345_CS_GPIO_Port, ADXL345_CS_Pin, GPIO_PIN_SET)

/* SPI 读写一个字节 */
static uint8_t ADXL345_SPI_Transfer(uint8_t tx)
{
    uint8_t rx = 0;
    HAL_SPI_TransmitReceive(&hspi2, &tx, &rx, 1, HAL_MAX_DELAY);
    return rx;
}

/* 写寄存器：地址最高位为 0，后面跟数据 */
static void ADXL345_WriteReg(uint8_t reg, uint8_t value)
{
    ADXL345_CS_LOW();
    ADXL345_SPI_Transfer(reg & 0x7F);   /* bit7=0 表示写 */
    ADXL345_SPI_Transfer(value);
    ADXL345_CS_HIGH();
}

/* 读寄存器：地址 bit7=1 表示读 */
static uint8_t ADXL345_ReadReg(uint8_t reg)
{
    uint8_t value;
    ADXL345_CS_LOW();
    ADXL345_SPI_Transfer(reg | 0x80);   /* bit7=1 表示读 */
    value = ADXL345_SPI_Transfer(0xFF); /* 发 dummy 读数据 */
    ADXL345_CS_HIGH();
    return value;
}

/* 多字节读：bit7=1(读), bit6=1(多字节) */
static void ADXL345_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    ADXL345_CS_LOW();
    ADXL345_SPI_Transfer(reg | 0xC0);   /* bit7=1 读, bit6=1 多字节 */
    for (uint8_t i = 0; i < len; i++)
    {
        buf[i] = ADXL345_SPI_Transfer(0xFF);
    }
    ADXL345_CS_HIGH();
}

uint8_t ADXL345_ReadDevID(void)
{
    return ADXL345_ReadReg(ADXL345_REG_DEVID);
}

int8_t ADXL345_Init(ADXL345_Range_t range, ADXL345_Rate_t rate)
{
    /* 1. 检查设备 ID */
    if (ADXL345_ReadDevID() != ADXL345_DEVID_VALUE)
    {
        return -1;
    }

    /* 2. 配置数据格式：全分辨率 + 指定量程 */
    uint8_t data_format = 0x08 | (range & 0x03);  /* bit3=1 全分辨率 */
    ADXL345_WriteReg(ADXL345_REG_DATA_FORMAT, data_format);

    /* 3. 配置输出数据速率 */
    ADXL345_WriteReg(ADXL345_REG_BW_RATE, rate & 0x0F);

    /* 4. 进入测量模式（POWER_CTL bit3=1） */
    ADXL345_WriteReg(ADXL345_REG_POWER_CTL, 0x08);

    return 0;
}

void ADXL345_ReadAccel(ADXL345_Data_t *data)
{
    uint8_t buf[6];
    ADXL345_ReadRegs(ADXL345_REG_DATAX0, buf, 6);

    data->x = (int16_t)(buf[1] << 8 | buf[0]);
    data->y = (int16_t)(buf[3] << 8 | buf[2]);
    data->z = (int16_t)(buf[5] << 8 | buf[4]);
}
