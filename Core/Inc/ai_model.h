#ifndef __AI_MODEL_H
#define __AI_MODEL_H

#include <stdint.h>

/* 故障类型（与训练标签一致） */
typedef enum {
    FAULT_NORMAL = 0,   /* 正常 */
    FAULT_IR     = 1,   /* 内圈故障 (Inner Race) */
    FAULT_BALL   = 2,   /* 滚动体故障 (Ball) */
    FAULT_OR     = 3    /* 外圈故障 (Outer Race) */
} FaultClass_t;

/* AI 推理结果 */
typedef struct {
    FaultClass_t fault;       /* 预测故障类别 */
    float        confidence;  /* 最高类别概率 (0~1) */
    float        probs[4];    /* 4 类概率 [Normal, IR, Ball, OR] */
} AiResult_t;

/**
 * @brief  初始化 AI 网络（创建 + 加载权重 + 分配激活内存）
 * @return 0=成功, -1=失败
 */
int8_t AI_Init(void);

/**
 * @brief  对一帧振动数据做故障分类推理
 * @param  data  1024 点 int16 振动原始数据（ADXL345 Z 轴）
 * @param  len   数据长度（必须为 1024）
 * @param  res   输出推理结果
 * @return 0=成功, -1=失败
 *
 * 预处理：与训练时一致，对每帧独立做 z-score 归一化
 *   x_norm = (x - mean) / (std + 1e-8)
 */
int8_t AI_Run(const int16_t *data, uint16_t len, AiResult_t *res);

/* 故障类别名称（用于串口打印） */
const char* AI_FaultName(FaultClass_t c);

#endif /* __AI_MODEL_H */
