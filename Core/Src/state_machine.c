/**
 * state_machine.c — 自适应预测性维护核心算法
 *
 * 核心思想：不使用固定阈值，而是每台设备自动学习自己的"正常状态"，
 * 然后用 z-score（偏离倍数）检测异常，实现通用性。
 *
 * 工作流程：
 *   1. 开机后前 50 秒：自动学习正常运行时 RMS 的均值μ和标准差σ
 *   2. 实时计算 z = (当前RMS - μ) / σ
 *   3. z<2 正常, 2<z<3 关注, 3<z<5 预警, 5<z<8 紧急, z>8 失效
 *   4. 基于退化趋势预测剩余寿命(RUL)，给出维护建议
 *
 * 优势：换不同电机/不同转速/不同传感器都能自动适应，无需调参。
 */
#include "state_machine.h"
#include "main.h"
#include "flash_store.h"
#include <math.h>

static StateOutput_t g_out;
static uint8_t g_confirm_count = 0;

/* ---------- 自适应基线学习 ---------- */
#define LEARN_SAMPLES  200   /* 学习 200 帧（每帧0.256s，共约50秒） */
static double g_sum_rms = 0, g_sum_rms2 = 0;  /* 累积和、累积平方和 */
static double g_sum_kurt = 0, g_sum_kurt2 = 0;
static double g_sum_temp = 0;
static uint32_t g_learn_cnt = 0;

/* RUL 退化历史（存组合退化分数，融合 RMS 和峭度） */
static float g_deg_hist[32];
static uint8_t g_hist_idx = 0, g_hist_cnt = 0;

static void update_led(HealthState_t s)
{
    BSP_LED_Off(LED_GREEN);
    BSP_LED_Off(LED_BLUE);
    BSP_LED_Off(LED_RED);
    switch (s) {
        case HEALTH_GOOD:     BSP_LED_On(LED_GREEN); g_out.led_color = 0; break;
        case HEALTH_WATCH:    BSP_LED_On(LED_BLUE);  g_out.led_color = 1; break;
        case HEALTH_WARN:     BSP_LED_On(LED_BLUE);  g_out.led_color = 1; break;
        case HEALTH_CRITICAL: BSP_LED_On(LED_RED);   g_out.led_color = 2; break;
        case HEALTH_FAILURE:  BSP_LED_On(LED_RED);   g_out.led_color = 2; break;
    }
}

/*
 * 自适应基线学习：
 *   累积 200 帧数据后，计算正常状态的均值(μ)和标准差(σ)
 *   均值 = Σx / n
 *   标准差 = sqrt(Σx²/n - (Σx/n)²)
 *   之后用 z = (x - μ) / σ 判断偏离程度
 */
static void baseline_learn(float rms, float kurt, float temp)
{
    if (g_out.baseline.learned) return;  /* 已学习完成，不再更新 */

    /* 累加计算（用 double 防止精度损失） */
    g_sum_rms  += rms;
    g_sum_rms2 += (double)rms * rms;
    g_sum_kurt += kurt;
    g_sum_kurt2 += (double)kurt * kurt;
    g_sum_temp += temp;
    g_learn_cnt++;

    if (g_learn_cnt >= LEARN_SAMPLES) {
        float n = (float)g_learn_cnt;
        g_out.baseline.rms_mean  = (float)(g_sum_rms / n);
        g_out.baseline.rms_std   = (float)sqrt(g_sum_rms2 / n - (g_sum_rms/n) * (g_sum_rms/n));
        g_out.baseline.kurt_mean = (float)(g_sum_kurt / n);
        g_out.baseline.kurt_std  = (float)sqrt(g_sum_kurt2 / n - (g_sum_kurt/n) * (g_sum_kurt/n));
        g_out.baseline.temp_mean = (float)(g_sum_temp / n);
        if (g_out.baseline.rms_std < 1.0f) g_out.baseline.rms_std = 1.0f;  /* 防除零 */
        if (g_out.baseline.kurt_std < 0.5f) g_out.baseline.kurt_std = 0.5f;
        g_out.baseline.learned = 1;
        g_out.baseline.sample_cnt = g_learn_cnt;
        /* 学习完成，保存到 Flash（掉电不丢失，下次开机直接加载） */
        FlashStore_SaveBaseline(&g_out.baseline);
    }
}

/*
 * RUL（剩余使用寿命）预测：
 *   综合 RMS 和峭度两个特征的 z-score，取最大值作为"退化分数"
 *   （峭度对早期冲击故障更敏感，RMS 对晚期磨损更敏感，两者互补）
 *   用最近 32 帧退化分数做线性回归，预测到达失效阈值(8.0)还需多久
 */
static uint32_t predict_rul(float z_rms, float kurt)
{
    /* 组合退化分数 = max(|z_rms|, |z_kurt|) */
    float z_kurt = (kurt - g_out.baseline.kurt_mean) / g_out.baseline.kurt_std;
    float deg = z_rms;
    if (z_kurt > deg) deg = z_kurt;
    if (deg < 0) deg = -deg;

    /* 环形缓冲：保存最近 32 帧退化分数 */
    g_deg_hist[g_hist_idx] = deg;
    g_hist_idx = (g_hist_idx + 1) % 32;
    if (g_hist_cnt < 32) g_hist_cnt++;
    if (g_hist_cnt < 8) return 9999;  /* 数据不足，不预测 */

    /* 最小二乘线性拟合：y = slope * x + intercept */
    float sx=0, sy=0, sxy=0, sxx=0;
    for (uint8_t i = 0; i < g_hist_cnt; i++) {
        float x = (float)i;
        float y = g_deg_hist[(g_hist_idx + i) % 32];
        sx += x; sy += y; sxy += x*y; sxx += x*x;
    }
    float n = (float)g_hist_cnt;
    float slope = (n*sxy - sx*sy) / (n*sxx - sx*sx);      /* 退化速率 */
    float intercept = (sy - slope*sx) / n;

    if (slope <= 0.001f) return 9999;  /* 几乎不退化，寿命很长 */

    /* 预测退化分数到达失效阈值(8.0)还需要多少帧 */
    float fail_level = 8.0f;
    float frames = (fail_level - intercept) / slope;
    if (frames <= 0) return 0;

    /* 每帧 0.256 秒，换算成小时 */
    float hours = frames * 0.256f / 3600.0f;
    if (hours > 9999) hours = 9999;
    return (uint32_t)hours;
}

void StateMachine_Init(void)
{
    g_out.health = HEALTH_GOOD;
    g_out.maint = MAINT_NONE;
    g_out.rul_hours = 9999;
    g_out.days_to_replace = 0;
    g_out.need_spare = 0;
    g_out.relay_on = 1;
    g_out.buzzer_on = 0;
    g_out.z_rms = 0;
    g_out.baseline.learned = 0;
    g_out.baseline.rms_mean = 0;
    g_out.baseline.rms_std = 1;
    g_out.baseline.kurt_mean = 3;
    g_out.baseline.kurt_std = 1;
    g_out.baseline.sample_cnt = 0;
    g_sum_rms = g_sum_rms2 = g_sum_kurt = g_sum_kurt2 = g_sum_temp = 0;
    g_learn_cnt = 0;
    g_confirm_count = 0;
    g_hist_cnt = 0;

    /* 尝试从 Flash 加载上次学习的基线（避免每次开机等 50 秒） */
    if (FlashStore_LoadBaseline(&g_out.baseline) == 0) {
        /* 加载成功，跳过学习阶段 */
        g_out.baseline.learned = 1;
    }

    update_led(HEALTH_GOOD);
}

const StateOutput_t* StateMachine_GetOutput(void)
{
    return &g_out;
}

/*
 * 状态机更新（每帧调用）：
 *   1. 累积学习基线
 *   2. 计算 z-score 偏离倍数
 *   3. 按偏离程度分级
 *   4. 预测 RUL 并给出维护建议
 */
const StateOutput_t* StateMachine_Update(const VibFeatures_t *feat, float temp)
{
    HealthState_t current = g_out.health;

    /* 步骤1：基线学习（前 200 帧） */
    baseline_learn(feat->rms, feat->kurtosis, temp);

    /* 学习阶段：不做故障判断 */
    if (!g_out.baseline.learned) {
        g_out.health = HEALTH_GOOD;
        g_out.maint = MAINT_MONITOR;
        g_out.z_rms = 0;
        return &g_out;
    }

    /* 步骤2：z-score = (当前RMS - 正常均值) / 正常标准差
     *   z=0 完全正常, z=2 轻微偏离, z=5 明显异常, z=8 危险 */
    g_out.z_rms = (feat->rms - g_out.baseline.rms_mean) / g_out.baseline.rms_std;

    /* 步骤2.5：基线长期漂移更新
     *   仅在健康状态（|z| < 1.5）下，用极小学习率(0.1%)微调均值和标准差，
     *   适应温度漂移、轴承磨损等缓慢变化；故障时(z 大)不更新，避免"学习故障"。 */
    if (g_out.z_rms > -1.5f && g_out.z_rms < 1.5f) {
        const float alpha = 0.001f;  /* 学习率：每帧只改 0.1% */
        g_out.baseline.rms_mean = (1.0f - alpha) * g_out.baseline.rms_mean + alpha * feat->rms;
        float diff = feat->rms - g_out.baseline.rms_mean;
        float var = g_out.baseline.rms_std * g_out.baseline.rms_std;
        var = (1.0f - alpha) * var + alpha * diff * diff;
        g_out.baseline.rms_std = sqrtf(var);
        if (g_out.baseline.rms_std < 1.0f) g_out.baseline.rms_std = 1.0f;
    }

    /* 步骤3：按 z-score 分级（自适应，不依赖绝对值） */
    uint8_t level = 0;
    if (g_out.z_rms > 8.0f) level = 4;       /* 失效：必须停机 */
    else if (g_out.z_rms > 5.0f) level = 3;  /* 紧急：尽快更换 */
    else if (g_out.z_rms > 3.0f) level = 2;  /* 预警：准备备件 */
    else if (g_out.z_rms > 2.0f) level = 1;  /* 关注：持续监测 */

    /* 步骤3.5：温度保护（电机过热会烧毁绕组，必须独立于振动判断）
     *   T > 80℃  → 至少关注级
     *   T > 90℃  → 至少紧急级
     *   T > 100℃ → 直接失效（过热停机保护） */
    if (temp > 100.0f) {
        level = 4;
    } else if (temp > 90.0f) {
        if (level < 3) level = 3;
    } else if (temp > 80.0f) {
        if (level < 1) level = 1;
    }

    HealthState_t target = (HealthState_t)level;

    /* 状态转换防抖：升级需连续 5 帧确认，降级立即生效 */
    if (target > current) {
        g_confirm_count++;
        if (g_confirm_count >= 5) {
            g_out.health = target;
            g_confirm_count = 0;
        }
    } else {
        g_confirm_count = 0;
        g_out.health = target;
    }

    /* 步骤4：RUL 预测 + 维护建议 */
    g_out.rul_hours = predict_rul(g_out.z_rms, feat->kurtosis);

    switch (g_out.health) {
        case HEALTH_GOOD:
            g_out.maint = MAINT_NONE; g_out.need_spare = 0; break;
        case HEALTH_WATCH:
            g_out.maint = MAINT_MONITOR; g_out.need_spare = 0; break;
        case HEALTH_WARN:
            /* RUL>30天给30天准备期，否则7天 */
            g_out.maint = MAINT_PREPARE; g_out.need_spare = 1;
            g_out.days_to_replace = (g_out.rul_hours > 720) ? 30 : 7; break;
        case HEALTH_CRITICAL:
            g_out.maint = MAINT_SCHEDULE; g_out.need_spare = 1;
            g_out.days_to_replace = (g_out.rul_hours > 168) ? 7 : 1; break;
        case HEALTH_FAILURE:
            g_out.maint = MAINT_URGENT; g_out.need_spare = 1;
            g_out.days_to_replace = 0; break;
    }

    /* 执行器控制：只有 FAILURE 才断继电器/响蜂鸣器（保护设备） */
    g_out.relay_on = (g_out.health == HEALTH_FAILURE) ? 0 : 1;
    g_out.buzzer_on = (g_out.health == HEALTH_FAILURE) ? 1 : 0;
    HAL_GPIO_WritePin(RELAY_CTRL_GPIO_Port, RELAY_CTRL_Pin, g_out.relay_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, g_out.buzzer_on ? GPIO_PIN_SET : GPIO_PIN_RESET);

    if (g_out.health != current) update_led(g_out.health);

    return &g_out;
}
