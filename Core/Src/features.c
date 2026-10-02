/**
 * features.c — 振动信号时域特征提取
 *
 * 从 1024 点振动数据中提取 4 个物理特征：
 *   - mean         : 均值（直流分量）
 *   - rms          : 均方根（反映振动烈度，整体能量）
 *   - crest_factor : 峰值因子（峰值/RMS，冲击类故障会升高）
 *   - kurtosis     : 峭度（四阶统计量，正常≈3，有冲击>4）
 *
 * 这些特征是所有滚动轴承通用的物理指标，不依赖特定设备。
 */
#include "features.h"
#include <math.h>

void Features_Extract(const int16_t *data, uint16_t len, VibFeatures_t *feat)
{
    if (len == 0) {
        feat->rms = 0; feat->kurtosis = 0; feat->crest_factor = 0; feat->mean = 0;
        return;
    }

    /* 1. 均值 = 直流分量（传感器零偏） */
    double sum = 0;
    for (uint16_t i = 0; i < len; i++) sum += data[i];
    double mean = sum / len;
    feat->mean = (float)mean;

    /* 2. RMS = sqrt(mean((x-mean)^2))，反映振动总能量 */
    double sum_sq = 0;
    int16_t peak = 0;  /* 峰值（绝对值最大） */
    for (uint16_t i = 0; i < len; i++) {
        double v = data[i] - mean;   /* 去直流 */
        sum_sq += v * v;
        int16_t abs_v = (v < 0) ? (int16_t)(-v) : (int16_t)v;
        if (abs_v > peak) peak = abs_v;
    }
    double rms = sqrt(sum_sq / len);
    feat->rms = (float)rms;

    /* 3. 峰值因子 = 峰值 / RMS，正常值约 3-4，有冲击时 > 6 */
    feat->crest_factor = (rms > 0.001f) ? (float)peak / (float)rms : 0;

    /* 4. 峭度 = E[(x-μ)^4] / σ^4
     *    正态分布峭度=3；轴承出现剥落/点蚀产生冲击时峭度会>4 */
    double sum_4th = 0;
    for (uint16_t i = 0; i < len; i++) {
        double v = data[i] - mean;
        double v2 = v * v;
        sum_4th += v2 * v2;   /* (x-μ)^4 */
    }
    double variance = sum_sq / len;   /* σ^2 */
    feat->kurtosis = (variance > 0.001) ? (float)(sum_4th / len / (variance * variance)) : 3.0f;
}
