# 电机预测性维护终端（PdM Edge Node）

基于 STM32U575 + 端侧 AI 的电机轴承故障预测系统。通过振动+温度信号实时监测电机健康状态，**自适应学习每台设备的正常基线**，在故障发生前提前预警并给出维护建议，实现"不停机、不降速、提前备备件"。

## 核心特性

- **自适应基线学习**：开机 50 秒自动学习当前设备的正常振动特征，无需手动调参
- **z-score 异常检测**：用偏离倍数（而非固定阈值）判断异常，适配不同电机/转速/传感器
- **RUL 剩余寿命预测**：基于退化趋势线性回归，预测还能运行多少小时
- **分级维护建议**：关注 → 预警（备备件）→ 紧急（安排停机）→ 失效（自动停机保护）
- **端侧 AI 推理**：1D-CNN 模型在 STM32 上直接运行，Flash 仅占 5.6%

## 硬件清单

| 模块 | 型号 | 数量 |
|---|---|---|
| 主控 | NUCLEO-U575ZI-Q (STM32U575ZIT6Q) | 1 |
| 振动传感器 | ADXL345（三轴加速度计，±16g） | 1 |
| 温度传感器 | DS18B20 | 1 |
| 继电器模块 | 5V 光耦隔离 | 1 |
| 蜂鸣器 | 有源 5V | 1 |

## 接线表

| STM32 引脚 | 功能 | 连接对象 |
|---|---|---|
| PB10 | SPI2_SCK | ADXL345 SCLK |
| PC1 | SPI2_MOSI | ADXL345 SDA |
| PC2 | SPI2_MISO | ADXL345 SDO |
| PC3 | GPIO Output (CS) | ADXL345 CS |
| PA0 | GPIO Output OD | DS18B20 DQ |
| PA4 | DAC1_OUT1 | 调速信号输出（备用） |
| PA6 | TIM3_CH1 输入捕获 | 电机转速脉冲 |
| PB1 | GPIO Output | 继电器控制 |
| PC6 | GPIO Output | 蜂鸣器控制 |
| PA9/PA10 | USART1 | ST-LINK VCP 串口调试 |

## 算法流程

```
ADXL345(4kHz采样) → 分帧(1024点) → 特征提取(RMS/峭度)
                                    ↓
              ┌─────────────────────────────────────────┐
              │  自适应基线学习(前50秒) → z-score检测    │  → 健康等级 + RUL
              └─────────────────────────────────────────┘
                                    ↓
              ┌─────────────────────────────────────────┐
              │  1D-CNN 推理(z-score归一化 → 4分类)      │  → 故障类型 + 置信度
              │  Normal / IR(内圈) / Ball(滚动体) / OR(外圈) │
              └─────────────────────────────────────────┘
                                    ↓
              ┌──────────────────────────────────────┐
              │ z<2  正常                            │
              │ z 2-3  关注(MONITOR)                 │
              │ z 3-5  预警(PREPARE + 备件提醒)       │
              │ z 5-8  紧急(SCHEDULE + 备件提醒)       │
              │ z>8   失效(继电器断电 + 蜂鸣器)        │
              └──────────────────────────────────────┘
                                         ↓
                          RUL 预测(线性拟合退化趋势)
```

## 编译

使用 CLion 或 CMake + arm-none-eabi-gcc：

```powershell
cd D:\STM32\blink_demo
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/gcc-arm-none-eabi.cmake ..
make -j
```

或直接在 CLion 中打开工程，点小锤子编译。

## 烧录

```powershell
.\flash.ps1
```

## 串口输出

```powershell
.\serial_monitor.ps1 -Port COMx -Baud 115200
```

示例输出：
```
ADXL345 DEVID = 0xE5, init = OK
DS18B20 init = OK
AI model init = OK
LEARNING... RMS= 180.2 (1/200)
...
RMS=  185.3 z= 0.3 T=42.1C | H=0 RUL=9999uh --- | AI=Normal(98%)
RMS=  450.3 z= 4.2 T=50.1C | H=2 RUL= 320uh PREPARE [SPARE] | AI=IR(87%)
```

## 项目结构

```
blink_demo/
├── Core/
│   ├── Inc/          # 头文件
│   └── Src/          # 源代码
│       ├── main.c           # 主程序
│       ├── adxl345.c        # ADXL345 振动驱动
│       ├── ds18b20.c        # DS18B20 温度驱动
│       ├── features.c       # 特征提取(RMS/峭度)
│       ├── state_machine.c  # 自适应检测+RUL预测
│       ├── control.c        # DAC调速+转速测量
│       └── ai_model.c       # 1D-CNN 故障分类推理
├── Drivers/          # STM32 HAL 库
├── Middlewares/ST/AI # X-CUBE-AI 运行时库
├── X-CUBE-AI/App     # AI 模型 C 代码
├── blink_demo.ioc    # CubeMX 配置
└── flash.ps1         # 烧录脚本
```

## 资源占用（含 AI 模型）

| 资源 | 用量 | 占比 |
|---|---|---|
| Flash (ROM) | 168.3 KB | 8.0% |
| RAM | 31.1 KB | 4.0% |

其中 AI 模型占：权重 103 KB（Flash）、激活缓冲 20.9 KB（RAM）。

## 开发工具

- STM32CubeMX 6.x（外设配置）
- X-CUBE-AI 10.2.1（AI 模型部署）
- CLion 2026（IDE）
- arm-none-eabi-gcc 13.3.1（编译器）
- OpenOCD 0.12（烧录调试）
