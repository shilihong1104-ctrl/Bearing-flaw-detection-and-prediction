"""数据准备:CWRU 轴承数据集 或 合成数据(兜底)。

端侧对齐:
  - 每帧 1024 点 (config.SAMPLE_LEN)
  - 4 类: Normal / IR / Ball / OR
  - 归一化在训练时由 utils.zscore_normalize 完成, 与端侧一致

两种数据源:
  1. cwru      —— 真实 CWRU .mat 文件(放入 data/ 目录,或自动下载镜像)
  2. synthetic —— 生成模拟振动信号,用于无网络/无数据时验证训练闭环
  3. auto      —— 有 cwru 用 cwru,否则 synthetic (默认)
"""
import sys
from pathlib import Path

import numpy as np
from scipy.io import loadmat

import config as C
from utils import zscore_normalize


# ------------------------------------------------------------------ #
#  CWRU 真实数据
# ------------------------------------------------------------------ #
def _find_de_signal(mat: dict) -> np.ndarray:
    """从 .mat 中找驱动端 (DE) 振动信号字段。"""
    for key in mat:
        if key.endswith("DE_time"):
            return np.asarray(mat[key]).ravel().astype(np.float32)
    # 退而求其次:取第一个一维信号数组
    for key, val in mat.items():
        if key.startswith("__"):
            continue
        arr = np.asarray(val)
        if arr.ndim <= 2 and arr.size > 1000:
            return arr.ravel().astype(np.float32)
    raise RuntimeError("未在 .mat 中找到振动信号字段")


def download_cwru(dest_dir: Path = C.DATA_DIR) -> bool:
    """尝试从社区镜像下载 CWRU 的 4 个 .mat 文件。成功返回 True。"""
    try:
        import urllib.request
    except ImportError:
        return False
    dest_dir.mkdir(parents=True, exist_ok=True)
    ok = True
    for cls, fname in C.CWRU_FILES.items():
        out = dest_dir / fname
        if out.exists() and out.stat().st_size > 1000:
            continue
        url = C.CWRU_BASE_URL + fname
        try:
            print(f"  下载 {cls}: {fname} ...", flush=True)
            urllib.request.urlretrieve(url, out)
        except Exception as e:
            print(f"  下载失败 {fname}: {e}", flush=True)
            ok = False
    return ok


def load_cwru(data_dir: Path = C.DATA_DIR) -> tuple:
    """读取 data/ 下的 CWRU .mat,滑窗切帧,返回 (X, y)。"""
    X_list, y_list = [], []
    missing = []
    for cls_idx, (cls, fname) in enumerate(C.CWRU_FILES.items()):
        fpath = data_dir / fname
        if not fpath.exists():
            missing.append(fname)
            continue
        mat = loadmat(str(fpath))
        sig = _find_de_signal(mat)
        frames = _sliding_window(sig, C.SAMPLE_LEN, overlap=C.SAMPLE_LEN // 2)
        X_list.append(frames)
        y_list.append(np.full(len(frames), cls_idx, dtype=np.int64))
        print(f"  {cls:8s} ({fname}): {len(frames)} 帧", flush=True)
    if missing:
        raise FileNotFoundError(
            f"缺少 CWRU 文件: {missing}; 请放入 {data_dir} 或切换 --source synthetic"
        )
    X = np.concatenate(X_list, axis=0).astype(np.float32)
    y = np.concatenate(y_list, axis=0)
    return X, y


def _sliding_window(sig: np.ndarray, length: int, overlap: int) -> np.ndarray:
    """滑动窗口切帧,overlap 为帧间重叠样本数。"""
    step = length - overlap
    n = (len(sig) - length) // step + 1
    idx = np.arange(n)[:, None] * step + np.arange(length)
    return sig[idx]


# ------------------------------------------------------------------ #
#  合成数据(兜底验证闭环)
# ------------------------------------------------------------------ #
def make_synthetic(n_per_class: int = 300, seed: int = C.RANDOM_SEED) -> tuple:
    """生成 4 类模拟振动信号,用于无真实数据时验证训练→部署闭环。

    每类有不同的特征频率成分,模型应能学到区分:
      Normal : 单一工频正弦 + 噪声
      IR     : 工频 + 内圈故障调制频率冲击
      Ball   : 工频 + 滚动体故障频率冲击
      OR     : 工频 + 外圈故障频率冲击
    """
    rng = np.random.default_rng(seed)
    fs = 12000.0                      # 12kHz, 与 CWRU 一致
    t = np.arange(C.SAMPLE_LEN) / fs
    f_running = 50.0                  # 转频
    # 各类特征频率(简化模拟)
    patterns = {
        0: [(f_running, 1.0)],                                   # Normal
        1: [(f_running, 1.0), (f_running * 5.4, 0.6)],           # IR
        2: [(f_running, 1.0), (f_running * 4.3, 0.6)],           # Ball
        3: [(f_running, 1.0), (f_running * 3.8, 0.6)],           # OR
    }
    X, y = [], []
    for cls, freqs in patterns.items():
        for _ in range(n_per_class):
            sig = np.zeros(C.SAMPLE_LEN, dtype=np.float32)
            for f, amp in freqs:
                # 给每帧加随机相位,模拟实际变化
                phase = rng.uniform(0, 2 * np.pi)
                sig += amp * np.sin(2 * np.pi * f * t + phase)
            # 故障类叠加周期性冲击衰减
            if cls > 0:
                f_impact = f_running * (4.0 + cls)
                env = np.exp(-((t % (1 / f_impact)) * f_impact * 2))
                sig += 0.5 * env * rng.standard_normal(C.SAMPLE_LEN).astype(np.float32)
            sig += 0.1 * rng.standard_normal(C.SAMPLE_LEN).astype(np.float32)
            X.append(sig)
            y.append(cls)
    X = np.stack(X).astype(np.float32)
    y = np.asarray(y, dtype=np.int64)
    return X, y


# ------------------------------------------------------------------ #
#  统一入口
# ------------------------------------------------------------------ #
def build_dataset(source: str = "auto") -> tuple:
    """返回 (X, y) numpy 数组。X shape: (N, SAMPLE_LEN), y: (N,)。

    source: 'auto' | 'cwru' | 'synthetic'
    """
    if source == "synthetic":
        print("[data] 使用合成数据 (synthetic)", flush=True)
        return make_synthetic()
    if source in ("cwru", "auto"):
        try:
            X, y = load_cwru()
            print(f"[data] 加载 CWRU 成功: {len(y)} 帧", flush=True)
            return X, y
        except FileNotFoundError:
            if source == "cwru":
                raise
            print("[data] CWRU 不可用,尝试自动下载...", flush=True)
            if download_cwru():
                X, y = load_cwru()
                print(f"[data] 下载并加载 CWRU 成功: {len(y)} 帧", flush=True)
                return X, y
            print("[data] 下载失败,回退到合成数据 (synthetic)", flush=True)
            return make_synthetic()
    raise ValueError(f"未知 source: {source}")


def to_model_input(X: np.ndarray) -> np.ndarray:
    """原始信号 → z-score 归一化 → 增加通道维, 适配 Conv1d (N, 1, 1024)。"""
    Xn = zscore_normalize(X)                # (N, 1024)
    return Xn[:, None, :].astype(np.float32)  # (N, 1, 1024)


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "auto"
    X, y = build_dataset(src)
    Xn = to_model_input(X)
    print(f"X raw : {X.shape}  range=[{X.min():.1f}, {X.max():.1f}]")
    print(f"X norm: {Xn.shape}  mean={Xn.mean():.3f} std={Xn.std():.3f}")
    print(f"y     : {y.shape}  classes={np.bincount(y)}")
