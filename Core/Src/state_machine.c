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
#include <math.h>

static StateOutput_t g_out;
static uint8_t g_confirm_count = 0;

/* ---------- 自适应基线学习 ---------- */
#define LEARN_SAMPLES  200   /* 学习 200 帧（每帧0.256s，共约50秒） */
static double g_sum_rms = 0, g_sum_rms2 = 0;  /* 累积和、累积平方和 */
static double g_sum_kurt = 0;
static double g_sum_temp = 0;
static uint32_t g_learn_cnt = 0;

/* RUL 退化历史 */
static float g_rms_hist[32];
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
    g_sum_temp += temp;
    g_learn_cnt++;

    if (g_learn_cnt >= LEARN_SAMPLES) {
        float n = (float)g_learn_cnt;
        g_out.baseline.rms_mean  = (float)(g_sum_rms / n);
        g_out.baseline.rms_std   = (float)sqrt(g_sum_rms2 / n - (g_sum_rms/n) * (g_sum_rms/n));
        g_out.baseline.kurt_mean = (float)(g_sum_kurt / n);
        g_out.baseline.temp_mean = (float)(g_sum_temp / n);
        if (g_out.baseline.rms_std < 1.0f) g_out.baseline.rms_std = 1.0f;  /* 防除零 */
        g_out.baseline.learned = 1;
        g_out.baseline.sample_cnt = g_learn_cnt;
    }
}

/*
 * RUL（剩余使用寿命）预测：
 *   用最近 32 帧 RMS 做线性回归，得到退化速率 slope
 *   失效阈值 = μ + 8σ（z=8 时认为即将失效）
 *   剩余帧数 = (失效阈值 - 截距) / slope
 *   换算成小时返回
 */
static uint32_t predict_rul(float current_rms)
{
    /* 环形缓冲：保存最近 32 帧 RMS */
    g_rms_hist[g_hist_idx] = current_rms;
    g_hist_idx = (g_hist_idx + 1) % 32;
    if (g_hist_cnt < 32) g_hist_cnt++;
    if (g_hist_cnt < 8) return 9999;  /* 数据不足，不预测 */

    /* 最小二乘线性拟合：y = slope * x + intercept */
    float sx=0, sy=0, sxy=0, sxx=0;
    for (uint8_t i = 0; i < g_hist_cnt; i++) {
        float x = (float)i;
        float y = g_rms_hist[(g_hist_idx + i) % 32];
        sx += x; sy += y; sxy += x*y; sxx += x*x;
    }
    float n = (float)g_hist_cnt;
    float slope = (n*sxy - sx*sy) / (n*sxx - sx*sx);      /* 退化速率 */
    float intercept = (sy - slope*sx) / n;

    if (slope <= 0.001f) return 9999;  /* 几乎不退化，寿命很长 */

    /* 预测到达失效阈值(μ+8σ)还需要多少帧 */
    float fail_level = g_out.baseline.rms_mean + 8.0f * g_out.baseline.rms_std;
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
    g_out.baseline.sample_cnt = 0;
    g_sum_rms = g_sum_rms2 = g_sum_kurt = g_sum_temp = 0;
    g_learn_cnt = 0;
    g_confirm_count = 0;
    g_hist_cnt = 0;

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

    /* 步骤3：按 z-score 分级（自适应，不依赖绝对值） */
    uint8_t level = 0;
    if (g_out.z_rms > 8.0f) level = 4;       /* 失效：必须停机 */
    else if (g_out.z_rms > 5.0f) level = 3;  /* 紧急：尽快更换 */
    else if (g_out.z_rms > 3.0f) level = 2;  /* 预警：准备备件 */
    else if (g_out.z_rms > 2.0f) level = 1;  /* 关注：持续监测 */

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
    g_out.rul_hours = predict_rul(feat->rms);

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
