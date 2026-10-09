/**
 * flash_store.c — 基线参数掉电保存
 *
 * 使用 STM32U575 Flash 最后一页（0x081FF000，8KB）存储自适应基线。
 * 这样重启后无需再等 50 秒学习，直接加载上次的基线即可检测。
 *
 * STM32U5 Flash 特性：
 *   - 页大小 8KB
 *   - 写之前必须先擦除（按页擦除）
 *   - 只能把 1 写成 0，不能把 0 写成 1（所以必须先擦除）
 */
#include "flash_store.h"
#include "stm32u5xx_hal.h"
#include <string.h>

/* 存储地址：Flash 最后一页（2MB Flash - 8KB） */
#define BASELINE_FLASH_ADDR   0x081FF000UL
#define FLASH_PAGE_SIZE       8192UL

int8_t FlashStore_SaveBaseline(const Baseline_t *bl)
{
    BaselineStore_t store;
    store.magic      = BASELINE_MAGIC;
    store.rms_mean   = bl->rms_mean;
    store.rms_std    = bl->rms_std;
    store.kurt_mean  = bl->kurt_mean;
    store.kurt_std   = bl->kurt_std;
    store.temp_mean  = bl->temp_mean;
    store.sample_cnt = bl->sample_cnt;
    store.reserved   = 0;

    /* 1. 解锁 Flash */
    HAL_FLASH_Unlock();

    /* 2. 擦除目标页 */
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Page      = (BASELINE_FLASH_ADDR - 0x08000000UL) / FLASH_PAGE_SIZE;
    erase.NbPages   = 1;

    uint32_t page_error = 0;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return -1;
    }

    /* 3. 按 128 位四字写入（STM32U5 编程粒度为 quad-word = 16 字节）
     *    HAL_FLASH_Program 的第三个参数是数据所在的内存地址 */
    uint32_t addr = BASELINE_FLASH_ADDR;
    uint16_t total = sizeof(BaselineStore_t);
    uint16_t qwords = (total + 15) / 16;  /* 向上取整到 16 字节 */

    /* 16 字节编程缓冲（清零，超出 total 的部分为 0） */
    uint8_t prog_buf[16];

    for (uint16_t i = 0; i < qwords; i++) {
        memset(prog_buf, 0, sizeof(prog_buf));
        uint16_t off = i * 16;
        uint16_t copy_len = (off + 16 <= total) ? 16 : (total - off);
        memcpy(prog_buf, (uint8_t *)&store + off, copy_len);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr, (uint32_t)prog_buf) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
        addr += 16;
    }

    /* 4. 锁定 Flash */
    HAL_FLASH_Lock();
    return 0;
}

int8_t FlashStore_LoadBaseline(Baseline_t *bl)
{
    BaselineStore_t *store = (BaselineStore_t *)BASELINE_FLASH_ADDR;

    /* 校验魔数，判断 Flash 中是否有有效基线 */
    if (store->magic != BASELINE_MAGIC) {
        return -1;
    }

    bl->rms_mean   = store->rms_mean;
    bl->rms_std    = store->rms_std;
    bl->kurt_mean  = store->kurt_mean;
    bl->kurt_std   = store->kurt_std;
    bl->temp_mean  = store->temp_mean;
    bl->sample_cnt = store->sample_cnt;
    bl->learned    = 1;
    return 0;
}
