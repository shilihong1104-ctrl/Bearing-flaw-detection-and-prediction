/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dac.h"
#include "icache.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "adxl345.h"
#include "ds18b20.h"
#include "features.h"
#include "state_machine.h"
#include "control.h"
#include "ai_model.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;

/* USER CODE BEGIN PV */
#define FRAME_LEN  1024                  /* 每帧采样点数 */
volatile int16_t g_z_buffer[FRAME_LEN]; /* Z 轴振动缓冲 */
volatile uint16_t g_buf_idx = 0;        /* 缓冲写入位置 */
volatile uint8_t g_frame_ready = 0;     /* 一帧采集完成标志 */
volatile int16_t g_temp_x10 = 0;        /* 温度 ×10 */
static uint32_t g_temp_conv_start = 0;   /* 温度转换启动时刻 */
static uint8_t  g_temp_converting = 0;   /* 温度转换进行中标志 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void SystemPower_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* printf 重定向到 COM1（USART1） */
extern UART_HandleTypeDef hcom_uart[];

int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
}

/* IWDG 独立看门狗（寄存器直接操作，无需 HAL IWDG 驱动）
 * LSI ≈ 32kHz, 预分频 /64, reload=1000 → 超时 ≈ 2s
 * 主循环必须在 2s 内喂狗，否则自动复位（满足 72h 连续运行硬约束） */
static void IWDG_Init(void)
{
  IWDG->KR  = 0x5555;   /* 解锁 PR/RLR 寄存器 */
  IWDG->PR  = 0x04;     /* 预分频 /64 */
  IWDG->RLR = 1000;     /* 重载值 → 2s 超时 */
  while (IWDG->SR != 0) { /* 等待 PVU/RVU 位清零 */ }
  IWDG->KR  = 0xCCCC;   /* 启动看门狗 */
  IWDG->KR  = 0xAAAA;   /* 首次喂狗 */
}

static inline void IWDG_Refresh(void)
{
  IWDG->KR = 0xAAAA;    /* 喂狗 */
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the System Power */
  SystemPower_Config();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* ============================================================
   * 外设初始化（CubeMX 自动生成）
   * MX_GPIO_Init   — GPIO（PB1继电器、PC6蜂鸣器、PC3片选等）
   * MX_SPI2_Init   — SPI2（PB10=SCK, PC1=MOSI, PC2=MISO，连 ADXL345）
   * MX_TIM6_Init   — 定时器6（4kHz 中断，驱动振动采样）
   * MX_TIM3_Init   — 定时器3（PA6输入捕获，测电机转速）
   * MX_DAC1_Init   — DAC（PA4输出0-3.3V，备用调速接口）
   * ============================================================ */
  MX_GPIO_Init();
  MX_ICACHE_Init();
  MX_SPI2_Init();
  MX_TIM6_Init();
  MX_DAC1_Init();
  MX_TIM3_Init();

  /* ---------- 应用层初始化 ---------- */

  /* ADXL345 振动传感器：SPI 通信，±16g 量程，3200Hz 采样 */
  int8_t ret = ADXL345_Init(ADXL345_RANGE_16G, ADXL345_RATE_3200HZ);
  uint8_t devid = ADXL345_ReadDevID();
  printf("ADXL345 DEVID = 0x%02X, init = %s\r\n", devid, (ret == 0) ? "OK" : "FAIL");

  /* DS18B20 温度传感器：1-Wire 单总线（PA0） */
  int8_t tret = DS18B20_Init();
  printf("DS18B20 init = %s\r\n", (tret == 0) ? "OK" : "FAIL");

  /* 状态机：自适应基线学习 + z-score 异常检测 + RUL 预测 */
  StateMachine_Init();

  /* AI 模型：1D-CNN 轴承故障分类（4 分类） */
  int8_t ai_ret = AI_Init();
  printf("AI model init = %s\r\n", (ai_ret == 0) ? "OK" : "FAIL");

  /* DAC 输出 100%（全速，不干预生产） */
  Control_SetSpeed(100);

  /* 启动中断：TIM6=4kHz采样, TIM3=转速脉冲捕获 */
  HAL_TIM_Base_Start_IT(&htim6);
  HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1);
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_BLUE);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* 启动独立看门狗（2s 超时，主循环喂狗） */
  IWDG_Init();
  printf("IWDG started (2s timeout)\r\n");

  /* ============================================================
   * 主循环（while 1）
   * 核心数据流：
   *   TIM6中断(4kHz) → 采满1024点 → g_frame_ready=1
   *   ↓
   *   主循环检测到帧就绪 → 提取特征(RMS/峭度) → 状态机更新
   *   ↓
   *   输出健康等级 + RUL + 维护建议（串口打印）
   * ============================================================ */
  static uint32_t temp_timer = 0;
  while (1)
  {
    /* B1 按键（PC13）：手动复位状态机（重新学习基线） */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET)
    {
      HAL_Delay(20);  /* 消抖 */
      while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {}
      HAL_Delay(20);
      StateMachine_Init();    /* 清空基线，重新学习 */
      Control_SetSpeed(100);  /* DAC 恢复全速 */
      printf("[KEY] State reset, re-learning baseline\r\n");
    }

    /* 一帧数据采集完成（1024点 ≈ 0.256秒） */
    if (g_frame_ready)
    {
      g_frame_ready = 0;

      /* 步骤1：从原始振动数据提取时域特征 */
      VibFeatures_t feat;
      Features_Extract((int16_t *)g_z_buffer, FRAME_LEN, &feat);

      /* 步骤2：状态机更新（自适应学习 + z-score 检测 + RUL 预测） */
      float temp = g_temp_x10 / 10.0f;
      const StateOutput_t *out = StateMachine_Update(&feat, temp);

      /* 步骤3：AI 故障分类推理（1D-CNN，4 分类） */
      AiResult_t ai_res;
      int8_t ai_ok = AI_Run((int16_t *)g_z_buffer, FRAME_LEN, &ai_res);

      /* 步骤4：串口输出结果 */
      const char *maint_str[] = {"---", "LEARN", "PREPARE", "SCHEDULE", "URGENT!"};
      if (!out->baseline.learned) {
          /* 前 50 秒：正在学习正常状态基线 */
          printf("LEARNING... RMS=%6.1f (%lu/200)\r\n", feat.rms,
                 (unsigned long)out->baseline.sample_cnt);
      } else {
          /* 基线已建立，输出实时健康状态 + AI 故障分类 */
          printf("RMS=%7.1f z=%5.1f T=%5.1fC | H=%d RUL=%4luh %s%s | AI=%s(%.0f%%)\r\n",
                 feat.rms, out->z_rms, temp,
                 out->health, out->rul_hours,
                 maint_str[out->maint],
                 out->need_spare ? " [SPARE]" : "",
                 (ai_ok == 0) ? AI_FaultName(ai_res.fault) : "ERR",
                 (ai_ok == 0) ? ai_res.confidence * 100.0f : 0.0f);
      }
    }

    /* 温度采集（非阻塞：启动转换 → 等 750ms → 读结果 → 每 5 秒一次） */
    if (!g_temp_converting) {
        if (HAL_GetTick() - temp_timer > 5000) {
            DS18B20_StartConversion();
            g_temp_conv_start = HAL_GetTick();
            g_temp_converting = 1;
        }
    } else {
        if (HAL_GetTick() - g_temp_conv_start > 750) {
            g_temp_x10 = DS18B20_ReadResultX10();
            temp_timer = HAL_GetTick();
            g_temp_converting = 0;
        }
    }

    /* 喂狗（必须在 2s 内执行） */
    IWDG_Refresh();
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
  RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV4;
  RCC_OscInitStruct.PLL.PLLM = 3;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 1;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Power Configuration
  * @retval None
  */
static void SystemPower_Config(void)
{

  /*
   * Disable the internal Pull-Up in Dead Battery pins of UCPD peripheral
   */
  HAL_PWREx_DisableUCPDDeadBattery();

  /*
   * Switch to SMPS regulator instead of LDO
   */
  if (HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }
/* USER CODE BEGIN PWR */
/* USER CODE END PWR */
}

/* USER CODE BEGIN 4 */
/* TIM3 输入捕获回调：测量电机转速脉冲周期 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
  {
    uint32_t period = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    Control_UpdateSpeed(period);
    __HAL_TIM_SET_COUNTER(htim, 0);
  }
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM17 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM17)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  if (htim->Instance == TIM6)
  {
    /* 4kHz 中断：读取 ADXL345 Z 轴，填入帧缓冲 */
    ADXL345_Data_t accel;
    ADXL345_ReadAccel(&accel);
    g_z_buffer[g_buf_idx++] = accel.z;
    if (g_buf_idx >= FRAME_LEN)
    {
      g_buf_idx = 0;
      g_frame_ready = 1;
    }
  }
  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
