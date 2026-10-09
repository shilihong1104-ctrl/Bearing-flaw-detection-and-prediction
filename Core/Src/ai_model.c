/**
 * ai_model.c — 1D-CNN 轴承故障分类推理封装
 *
 * 模型：CWRU 数据集训练的 1D-CNN，4 分类
 *   输入：1024 点振动信号（z-score 归一化后的 float）
 *   输出：4 类 logits → softmax 得到概率
 *
 * 预处理必须与训练时一致（data_prep.py 的 normalize_per_sample）：
 *   mean = Σx / n
 *   std  = sqrt(Σ(x-mean)² / n)
 *   x_norm = (x - mean) / (std + 1e-8)
 */
#include "ai_model.h"
#include "bearing_cnn.h"
#include "bearing_cnn_data.h"
#include <math.h>
#include <string.h>

/* 网络句柄 */
static ai_handle g_network = AI_HANDLE_NULL;

/* 激活缓冲区（模型推理时的中间计算内存）
 * 大小由 bearing_cnn_data_params.h 定义：20864 字节
 * 必须 4 字节对齐 */
static AI_ALIGNED(4) uint8_t g_activations[AI_BEARING_CNN_DATA_ACTIVATIONS_SIZE];

/* 输入/输出缓冲区指针（由网络分配在激活区内） */
static ai_buffer *g_input  = NULL;
static ai_buffer *g_output = NULL;

/* 故障名称 */
static const char * const fault_names[] = {
    "Normal", "IR", "Ball", "OR"
};

const char* AI_FaultName(FaultClass_t c)
{
    if (c < 0 || c > 3) return "?";
    return fault_names[c];
}

/* ---------- softmax ---------- */
static void softmax(const float *logits, uint16_t n, float *probs)
{
    float max_val = logits[0];
    for (uint16_t i = 1; i < n; i++) {
        if (logits[i] > max_val) max_val = logits[i];
    }
    float sum = 0;
    for (uint16_t i = 0; i < n; i++) {
        probs[i] = expf(logits[i] - max_val);
        sum += probs[i];
    }
    for (uint16_t i = 0; i < n; i++) {
        probs[i] /= sum;
    }
}

/* ---------- 初始化网络 ---------- */
int8_t AI_Init(void)
{
    ai_error err;

    /* 1. 创建并初始化网络
     *    activations: 应用提供的激活缓冲区
     *    weights:     由 bearing_cnn_data 提供的权重 */
    const ai_handle acts[] = { g_activations };
    err = ai_bearing_cnn_create_and_init(&g_network, acts, NULL);
    if (err.type != AI_ERROR_NONE) {
        return -1;
    }

    /* 2. 获取输入/输出缓冲区指针 */
    ai_u16 n_in, n_out;
    g_input  = ai_bearing_cnn_inputs_get(g_network, &n_in);
    g_output = ai_bearing_cnn_outputs_get(g_network, &n_out);

    if (!g_input || !g_output || n_in != 1 || n_out != 1) {
        return -1;
    }

    /* 校验输入尺寸：1024 floats = 4096 bytes */
    if (g_input[0].size != AI_BEARING_CNN_IN_1_SIZE) {
        return -1;
    }

    return 0;
}

/* ---------- 推理一帧 ---------- */
int8_t AI_Run(const int16_t *data, uint16_t len, AiResult_t *res)
{
    if (!g_network || !g_input || !g_output || len != 1024) {
        return -1;
    }

    /* 1. z-score 归一化（与训练一致） */
    float *in_buf = (float *)g_input[0].data;
    double sum = 0, sum_sq = 0;
    for (uint16_t i = 0; i < len; i++) {
        double v = (double)data[i];
        sum += v;
        sum_sq += v * v;
    }
    float mean = (float)(sum / len);
    float var  = (float)(sum_sq / len - (double)mean * mean);
    if (var < 0) var = 0;
    float std  = sqrtf(var) + 1e-8f;

    for (uint16_t i = 0; i < len; i++) {
        in_buf[i] = ((float)data[i] - mean) / std;
    }

    /* 2. 运行推理 */
    ai_i32 n_batch = ai_bearing_cnn_run(g_network, g_input, g_output);
    if (n_batch != 1) {
        return -1;
    }

    /* 3. 读取输出（4 个 logits）并 softmax */
    float *out_buf = (float *)g_output[0].data;
    float probs[4];
    softmax(out_buf, 4, probs);

    /* 4. 找最大概率类别 */
    uint8_t best = 0;
    for (uint8_t i = 1; i < 4; i++) {
        if (probs[i] > probs[best]) best = i;
    }

    res->fault      = (FaultClass_t)best;
    res->confidence = probs[best];
    memcpy(res->probs, probs, sizeof(probs));

    return 0;
}
