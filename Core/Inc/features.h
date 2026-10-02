#ifndef __FEATURES_H
#define __FEATURES_H

#include <stdint.h>

/* 振动特征结构体 */
typedef struct {
    float rms;           /* 均方根（振动烈度） */
    float kurtosis;      /* 峭度（冲击特征，正常≈3，故障>4） */
    float crest_factor;  /* 峰值因子 = 峰值/RMS */
    float mean;          /* 直流分量 */
} VibFeatures_t;

/**
 * @brief  从一帧振动数据中提取特征
 * @param  data   原始加速度数据数组
 * @param  len    数据长度
 * @param  feat   输出特征
 */
void Features_Extract(const int16_t *data, uint16_t len, VibFeatures_t *feat);

#endif /* __FEATURES_H */
