#ifndef __FLASH_STORE_H
#define __FLASH_STORE_H

#include <stdint.h>
#include "state_machine.h"

/* 魔数：用于校验 Flash 中的基线数据是否有效 */
#define BASELINE_MAGIC  0x424C534Eu   /* 'BLSN' = BaseLine Saved */

/* Flash 中存储的基线结构 */
typedef struct {
    uint32_t magic;        /* 魔数校验 */
    float    rms_mean;     /* 正常 RMS 均值 */
    float    rms_std;      /* 正常 RMS 标准差 */
    float    kurt_mean;    /* 正常峭度均值 */
    float    kurt_std;     /* 正常峭度标准差 */
    float    temp_mean;    /* 正常温度均值 */
    uint32_t sample_cnt;   /* 学习样本数 */
    uint32_t reserved;     /* 保留（对齐用） */
} BaselineStore_t;

/**
 * @brief  把当前基线保存到 Flash（掉电不丢失）
 * @param  bl  要保存的基线数据
 * @return 0=成功，-1=失败
 */
int8_t FlashStore_SaveBaseline(const Baseline_t *bl);

/**
 * @brief  从 Flash 加载基线
 * @param  bl  输出基线数据
 * @return 0=成功（数据有效），-1=Flash 中无有效基线
 */
int8_t FlashStore_LoadBaseline(Baseline_t *bl);

#endif /* __FLASH_STORE_H */
