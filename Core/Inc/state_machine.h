#ifndef __STATE_MACHINE_H
#define __STATE_MACHINE_H

#include <stdint.h>
#include "features.h"

/* 健康等级 */
typedef enum {
    HEALTH_GOOD       = 0,
    HEALTH_WATCH      = 1,
    HEALTH_WARN       = 2,
    HEALTH_CRITICAL   = 3,
    HEALTH_FAILURE    = 4
} HealthState_t;

typedef enum {
    MAINT_NONE     = 0,
    MAINT_MONITOR  = 1,
    MAINT_PREPARE  = 2,
    MAINT_SCHEDULE = 3,
    MAINT_URGENT   = 4
} MaintAction_t;

/* 自适应基线（每台设备自动学习） */
typedef struct {
    float rms_mean;      /* 正常 RMS 均值 */
    float rms_std;       /* 正常 RMS 标准差 */
    float kurt_mean;     /* 正常峭度均值 */
    float temp_mean;     /* 正常温度均值 */
    uint8_t learned;     /* 是否已完成基线学习 */
    uint32_t sample_cnt; /* 已学习样本数 */
} Baseline_t;

typedef struct {
    HealthState_t health;
    MaintAction_t maint;
    uint32_t rul_hours;
    uint8_t days_to_replace;
    uint8_t need_spare;
    uint8_t relay_on;
    uint8_t buzzer_on;
    uint8_t led_color;
    float z_rms;         /* RMS 偏离倍数（z-score） */
    Baseline_t baseline; /* 当前基线 */
} StateOutput_t;

void StateMachine_Init(void);
const StateOutput_t* StateMachine_Update(const VibFeatures_t *feat, float temp);
const StateOutput_t* StateMachine_GetOutput(void);

#endif /* __STATE_MACHINE_H */
