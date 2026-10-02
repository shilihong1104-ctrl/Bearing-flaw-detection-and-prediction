/**
 * ds18b20.c — DS18B20 温度传感器驱动
 *
 * 通信协议：1-Wire 单总线（开漏输出 + 上拉电阻）
 * 接线：DQ=PA0
 * 功能：
 *   - DS18B20_Init()        : 复位 + 检测传感器是否存在
 *   - DS18B20_ReadTempX10() : 启动转换并读取温度（单位 0.1℃）
 *
 * 说明：DS18B20 转换一次温度约需 750ms，主循环每 5 秒读一次。
 */
#include "ds18b20.h"

/* DQ 引脚操作宏（开漏模式，输出低=拉低总线，输出高=释放总线） */
#define DQ_LOW()   HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_RESET)
#define DQ_HIGH()  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_SET)
#define DQ_READ()  HAL_GPIO_ReadPin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin)

/* 微秒级延时（160MHz 主频，经验值） */
static void delay_us(uint32_t us)
{
    uint32_t ticks = us * 16;  /* 160MHz / 10 = 16 ticks/μs 近似 */
    while (ticks--) { __NOP(); }
}

/* 发送复位脉冲并检测存在脉冲 */
static int8_t ds18b20_reset(void)
{
    DQ_LOW();
    delay_us(480);
    DQ_HIGH();
    delay_us(70);
    GPIO_PinState present = DQ_READ();
    delay_us(410);
    return (present == GPIO_PIN_RESET) ? 0 : -1;
}

/* 写一个位 */
static void ds18b20_write_bit(uint8_t bit)
{
    DQ_LOW();
    if (bit) {
        delay_us(6);
        DQ_HIGH();
        delay_us(64);
    } else {
        delay_us(60);
        DQ_HIGH();
        delay_us(10);
    }
}

/* 读一个位 */
static uint8_t ds18b20_read_bit(void)
{
    uint8_t bit;
    DQ_LOW();
    delay_us(6);
    DQ_HIGH();
    delay_us(9);
    bit = (DQ_READ() == GPIO_PIN_SET) ? 1 : 0;
    delay_us(55);
    return bit;
}

/* 写一个字节 */
static void ds18b20_write_byte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        ds18b20_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

/* 读一个字节 */
static uint8_t ds18b20_read_byte(void)
{
    uint8_t byte = 0;
    for (uint8_t i = 0; i < 8; i++) {
        byte >>= 1;
        if (ds18b20_read_bit()) byte |= 0x80;
    }
    return byte;
}

int8_t DS18B20_Init(void)
{
    if (ds18b20_reset() != 0) return -1;
    /* 写 Skip ROM + 写暂存器配置 12 位精度 */
    ds18b20_reset();
    ds18b20_write_byte(0xCC);  /* Skip ROM */
    ds18b20_write_byte(0x4E);  /* Write Scratchpad */
    ds18b20_write_byte(0x00);  /* TH */
    ds18b20_write_byte(0x00);  /* TL */
    ds18b20_write_byte(0x7F);  /* 12-bit resolution */
    return 0;
}

int16_t DS18B20_ReadTempX10(void)
{
    if (ds18b20_reset() != 0) return -999;

    /* 启动温度转换 */
    ds18b20_write_byte(0xCC);  /* Skip ROM */
    ds18b20_write_byte(0x44);  /* Convert T */
    HAL_Delay(750);            /* 12-bit 转换最长 750ms */

    if (ds18b20_reset() != 0) return -999;

    /* 读取暂存器 */
    ds18b20_write_byte(0xCC);  /* Skip ROM */
    ds18b20_write_byte(0xBE);  /* Read Scratchpad */

    uint8_t lsb = ds18b20_read_byte();
    uint8_t msb = ds18b20_read_byte();

    int16_t raw = (msb << 8) | lsb;
    /* 12-bit 分辨率，0.0625℃/LSB，×10 后 = 0.625/LSB */
    int16_t temp_x10 = (int16_t)((float)raw * 0.625f);
    return temp_x10;
}
