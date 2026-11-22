"""1D-CNN 轴承故障分类模型。

结构与端侧部署反推一致 (X-CUBE-AI 生成 bearing_cnn):
  - N_NODES = 5  →  Conv1+ReLU, Conv2+ReLU, Conv3+ReLU, GAP, Linear
  - 权重 ≈ 103KB (25764 float), 与 bearing_cnn_data_params.h 的 103056 字节吻合
  - 输入: (B, 1, 1024)  z-score 归一化后的振动信号
  - 输出: (B, 4)  logits (softmax 由调用方处理, 与端侧一致)
"""
import torch
import torch.nn as nn

import config as C


class BearingCNN(nn.Module):
    def __init__(self, num_classes: int = C.NUM_CLASSES,
                 in_channels: int = C.MODEL_IN_CHANNELS):
        super().__init__()
        ch = C.MODEL_CONV_CHANNELS      # [8, 16, 32]
        ks = C.MODEL_KERNELS            # [128, 64, 32]
        st = C.MODEL_STRIDES            # [8, 4, 2]

        self.features = nn.Sequential(
            # Block 1
            nn.Conv1d(in_channels, ch[0], kernel_size=ks[0], stride=st[0],
                      padding=ks[0] // 2),
            nn.ReLU(),
            nn.MaxPool1d(2),
            # Block 2
            nn.Conv1d(ch[0], ch[1], kernel_size=ks[1], stride=st[1],
                      padding=ks[1] // 2),
            nn.ReLU(),
            nn.MaxPool1d(2),
            # Block 3
            nn.Conv1d(ch[1], ch[2], kernel_size=ks[2], stride=st[2],
                      padding=ks[2] // 2),
            nn.ReLU(),
            nn.AdaptiveAvgPool1d(1),   # Global Average Pooling
        )
        self.classifier = nn.Linear(ch[2], num_classes)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.features(x)          # (B, 32, 1)
        x = torch.flatten(x, 1)       # (B, 32)
        x = self.classifier(x)        # (B, 4) logits
        return x


def count_parameters(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters() if p.requires_grad)


if __name__ == "__main__":
    # 自检: 打印结构 + 参数量 + 一次前向
    net = BearingCNN()
    n_params = count_parameters(net)
    print(f"Model: BearingCNN, params={n_params} "
          f"({n_params * 4 / 1024:.1f} KB)")
    x = torch.randn(2, 1, C.SAMPLE_LEN)
    y = net(x)
    print(f"Input  shape: {tuple(x.shape)}")
    print(f"Output shape: {tuple(y.shape)}  (expected (2, {C.NUM_CLASSES}))")
    assert y.shape == (2, C.NUM_CLASSES), "输出维度不匹配!"
    print("Forward pass OK.")
