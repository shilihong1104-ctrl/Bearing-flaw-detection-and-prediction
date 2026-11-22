# 训练流程 —— 端侧轴承故障检测

本目录提供从 **数据 → 训练 → 导出 → 部署** 的最小可复现流程,与仓库根目录的
STM32 端侧部署代码(`Core/` + `X-CUBE-AI/`)构成完整全链路。

```
data (.mat)  →  data_prep.py  →  train.py  →  best_model.pt
                                              ↓
                                        export.py  →  best_model.onnx
                                              ↓
                              STM32Cube.AI (X-CUBE-AI) 生成 C 代码
                                              ↓
                              X-CUBE-AI/App/bearing_cnn.c  →  板端推理
```

## 1. 环境准备

```bash
pip install -r requirements.txt
```

依赖:`torch` `numpy` `scipy` `scikit-learn` `matplotlib` `onnx` `tqdm`(可选)。

> 本仓库已在 Python 3.10 + PyTorch 2.x (CPU) 验证通过。

## 2. 数据

两种数据源,通过 `--source` 切换:

| source | 说明 |
|--------|------|
| `auto` | 默认。有 CWRU 用真实数据,否则回退合成数据 |
| `cwru` | 真实 CWRU 轴承数据集(12kHz 驱动端) |
| `synthetic` | 生成的模拟振动信号,用于无网络时快速验证闭环 |

### CWRU 真实数据

把 4 个 `.mat` 文件放入 `data/` 目录(脚本也会尝试自动下载):

| 类别 | 文件 | 说明 |
|------|------|------|
| Normal | `97.mat` | 正常 |
| IR | `105.mat` | 内圈故障 (Inner Race) |
| Ball | `118.mat` | 滚动体故障 (Ball) |
| OR | `130.mat` | 外圈故障 (Outer Race) |

滑窗切帧:每帧 1024 点,重叠 512 点。

### 合成数据(兜底)

`--source synthetic` 生成 4 类各 300 帧模拟信号,每类含不同特征频率成分。
仅用于验证训练→导出→部署闭环是否跑通,**正式结果请用 CWRU 真实数据**。

## 3. 训练

```bash
# 快速验证闭环(合成数据,几十秒)
python train.py --source synthetic --epochs 5

# 正式训练(CWRU)
python train.py --source cwru --epochs 30
```

输出到 `runs/<run_name>/`:

| 文件 | 内容 |
|------|------|
| `best_model.pt` | 最佳验证精度权重 |
| `training_curves.png` | loss / accuracy 曲线 |
| `confusion_matrix.png` | 测试集混淆矩阵 |
| `report.txt` | 分类报告 + 参数量 + 精度 |
| `history.json` | 每 epoch 指标 |

## 4. 导出 ONNX(供 X-CUBE-AI)

```bash
python export.py                      # 自动找最新 best_model.pt
python export.py --ckpt runs/xxx/best_model.pt
```

生成 `best_model.onnx`。在 **STM32Cube.AI** 中导入该 ONNX,即可生成
与 `X-CUBE-AI/App/bearing_cnn.c/.h` 同结构的端侧推理代码。

## 5. 与端侧部署的对齐约定

训练与部署必须严格一致,否则分布不匹配:

| 项 | 训练 | 端侧 | 一致 |
|----|------|------|------|
| 输入长度 | 1024 (`config.SAMPLE_LEN`) | `AI_BEARING_CNN_IN_1_SIZE` = 1024 | ✅ |
| 输入通道 | 1 | `AI_BEARING_CNN_IN_1_CHANNEL` = 1 | ✅ |
| 输出类别 | 4 (`Normal/IR/Ball/OR`) | 4 logits → softmax | ✅ |
| 归一化 | `utils.zscore_normalize` 每帧 z-score | `ai_model.c` 每帧 z-score | ✅ |
| 模型结构 | 3×Conv1d + GAP + Linear (≈25.8K 参数) | `bearing_cnn.h` N_NODES=5, 权重 103KB | ✅ |

> 模型结构由端侧 `X-CUBE-AI/App/bearing_cnn_data_params.h`
> (权重 103056 字节 ≈ 25764 float)反推得到,与本目录 `model.py` 吻合。

## 6. 文件说明

```
training/
├── README.md          本文件
├── requirements.txt   依赖
├── config.py          集中配置(与端侧单一数据源对齐)
├── model.py           1D-CNN 模型定义
├── data_prep.py       数据下载/读取/切帧/合成
├── train.py           训练入口
├── export.py          导出 ONNX
├── utils.py           z-score 归一化 / 绘图 / 种子
├── data/              CWRU .mat(未提交)
└── runs/              训练输出(未提交)
```
