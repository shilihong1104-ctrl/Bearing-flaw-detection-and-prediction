"""集中配置 —— 与端侧部署 (Core/Inc/ai_model.h) 保持单一数据源对齐。

修改这里的参数后,data_prep / model / train / export 会自动同步,
避免训练与部署的预处理、输入输出尺寸不一致。
"""
from pathlib import Path

# ---------- 路径 ----------
ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / "data"            # CWRU 原始 .mat 放这里
RUNS_DIR = ROOT / "runs"             # 训练输出(模型/曲线/报告)
RUNS_DIR.mkdir(parents=True, exist_ok=True)
DATA_DIR.mkdir(parents=True, exist_ok=True)

# ---------- 数据 ----------
SAMPLE_LEN = 1024          # 每帧振动信号长度(必须与端侧 AI_BEARING_CNN_IN_1_SIZE 一致)
NUM_CLASSES = 4            # 类别数: Normal / IR / Ball / OR
CLASS_NAMES = ["Normal", "IR", "Ball", "OR"]
# CWRU 4 类故障对应的 .mat 文件名(12kHz 驱动端,0.007 英寸故障)
CWRU_FILES = {
    "Normal": "97.mat",
    "IR": "105.mat",       # 内圈故障
    "Ball": "118.mat",     # 滚动体故障
    "OR": "130.mat",       # 外圈故障
}
# CWRU 下载镜像(官方站点需表单,用社区整理的镜像)
CWRU_BASE_URL = "https://github.com/cathaloruaidh/cwru-bearing-data/raw/master/"

# ---------- 模型 ----------
# 与端侧反推一致: 3×Conv1d + GAP + Linear, 参数量≈25.8K (≈103KB 权重)
MODEL_IN_CHANNELS = 1
MODEL_CONV_CHANNELS = [8, 16, 32]
MODEL_KERNELS = [128, 64, 32]
MODEL_STRIDES = [8, 4, 2]

# ---------- 训练 ----------
BATCH_SIZE = 64
EPOCHS = 30
LR = 1e-3
WEIGHT_DECAY = 1e-4
RANDOM_SEED = 42
VAL_SPLIT = 0.2
TEST_SPLIT = 0.2

# ---------- 预处理(必须与端侧 ai_model.c 一致)----------
# x_norm = (x - mean) / (std + 1e-8)   每帧独立 z-score 归一化
NORM_EPS = 1e-8
