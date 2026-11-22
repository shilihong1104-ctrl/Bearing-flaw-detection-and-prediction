"""通用工具:z-score 归一化、种子固定、绘图。

z-score 实现必须与端侧 Core/Src/ai_model.c 中的 AI_Run 完全一致,
否则训练与部署的分布不匹配。
"""
import os
import random
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")  # 无界面环境,写文件用
import matplotlib.pyplot as plt

import config as C


def set_seed(seed: int = C.RANDOM_SEED) -> None:
    """固定随机种子,保证可复现。"""
    random.seed(seed)
    np.random.seed(seed)
    os.environ["PYTHONHASHSEED"] = str(seed)
    try:
        import torch
        torch.manual_seed(seed)
        torch.cuda.manual_seed_all(seed)
        torch.backends.cudnn.deterministic = True
        torch.backends.cudnn.benchmark = False
    except ImportError:
        pass


def zscore_normalize(x: np.ndarray, eps: float = C.NORM_EPS) -> np.ndarray:
    """每帧(最后一维)独立 z-score 归一化。

    与端侧 ai_model.c 对齐:
        mean = sum(x) / n
        var  = sum(x^2)/n - mean^2
        std  = sqrt(var) + eps
        x_norm = (x - mean) / std
    """
    mean = x.mean(axis=-1, keepdims=True)
    var = x.var(axis=-1, keepdims=True)  # np.var 分母为 n,与端侧一致
    std = np.sqrt(var) + eps
    return (x - mean) / std


def plot_confusion_matrix(cm: np.ndarray, class_names, save_path: Path) -> None:
    """绘制混淆矩阵并保存。"""
    fig, ax = plt.subplots(figsize=(5, 4))
    im = ax.imshow(cm, interpolation="nearest", cmap=plt.cm.Blues)
    ax.figure.colorbar(im, ax=ax)
    ax.set(
        xticks=np.arange(cm.shape[1]),
        yticks=np.arange(cm.shape[0]),
        xticklabels=class_names,
        yticklabels=class_names,
        title="Confusion Matrix",
        ylabel="True label",
        xlabel="Predicted label",
    )
    thresh = cm.max() / 2.0
    for i in range(cm.shape[0]):
        for j in range(cm.shape[1]):
            ax.text(j, i, format(cm[i, j], "d"),
                    ha="center", va="center",
                    color="white" if cm[i, j] > thresh else "black")
    fig.tight_layout()
    fig.savefig(save_path, dpi=150)
    plt.close(fig)


def plot_training_curves(history: dict, save_path: Path) -> None:
    """绘制 loss / accuracy 曲线。"""
    fig, axes = plt.subplots(1, 2, figsize=(10, 4))
    epochs = range(1, len(history["train_loss"]) + 1)
    axes[0].plot(epochs, history["train_loss"], label="train")
    axes[0].plot(epochs, history["val_loss"], label="val")
    axes[0].set_title("Loss"); axes[0].set_xlabel("Epoch"); axes[0].legend()
    axes[1].plot(epochs, history["train_acc"], label="train")
    axes[1].plot(epochs, history["val_acc"], label="val")
    axes[1].set_title("Accuracy"); axes[1].set_xlabel("Epoch"); axes[1].legend()
    fig.tight_layout()
    fig.savefig(save_path, dpi=150)
    plt.close(fig)
