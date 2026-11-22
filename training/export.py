"""导出 ONNX —— 供 STM32Cube.AI (X-CUBE-AI) 生成端侧 C 代码。

用法:
    python export.py                         # 导出 runs/ 下最新的 best_model
    python export.py --ckpt runs/run_xxx/best_model.pt

流程:
    PyTorch (.pt)  →  ONNX  →  STM32Cube.AI 导入  →  生成 bearing_cnn.c/.h
    (与仓库现有的 X-CUBE-AI/App/bearing_cnn* 对齐)
"""
import argparse
import glob
import os
from pathlib import Path

import numpy as np
import torch

import config as C
from model import BearingCNN


def find_latest_ckpt() -> Path:
    """找 runs/ 下最新的 best_model.pt。"""
    files = sorted(glob.glob(str(C.RUNS_DIR / "*/best_model.pt")),
                   key=os.path.getmtime)
    if not files:
        raise FileNotFoundError(
            f"未找到 best_model.pt,请先运行 train.py (已查找 {C.RUNS_DIR})"
        )
    return Path(files[-1])


def main():
    parser = argparse.ArgumentParser(description="导出 ONNX 供 X-CUBE-AI 使用")
    parser.add_argument("--ckpt", default=None, help="best_model.pt 路径")
    parser.add_argument("--out", default=None, help="输出 .onnx 路径")
    args = parser.parse_args()

    ckpt_path = Path(args.ckpt) if args.ckpt else find_latest_ckpt()
    out_path = Path(args.out) if args.out else ckpt_path.parent / "best_model.onnx"

    print(f"[export] 加载: {ckpt_path}", flush=True)
    model = BearingCNN()
    ckpt = torch.load(str(ckpt_path), map_location="cpu", weights_only=False)
    model.load_state_dict(ckpt["model_state"])
    model.eval()

    dummy = torch.randn(1, C.MODEL_IN_CHANNELS, C.SAMPLE_LEN)
    torch.onnx.export(
        model, dummy, str(out_path),
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch"}, "output": {0: "batch"}},
        opset_version=11,
    )
    print(f"[export] ONNX 已保存: {out_path} ({out_path.stat().st_size} bytes)",
          flush=True)

    # 验证 ONNX 与 PyTorch 输出一致
    try:
        import onnxruntime as ort
        sess = ort.InferenceSession(str(out_path),
                                    providers=["CPUExecutionProvider"])
        onnx_out = sess.run(None, {"input": dummy.numpy()})[0]
        with torch.no_grad():
            pt_out = model(dummy).numpy()
        diff = np.abs(onnx_out - pt_out).max()
        print(f"[export] PyTorch vs ONNX 最大输出差异: {diff:.2e} "
              f"({'OK' if diff < 1e-4 else 'WARN'})", flush=True)
    except ImportError:
        print("[export] (未安装 onnxruntime,跳过数值一致性验证;可用 onnx 检查结构)",
              flush=True)
        try:
            import onnx
            m = onnx.load(str(out_path))
            onnx.checker.check_model(m)
            print("[export] ONNX 结构检查通过", flush=True)
        except ImportError:
            pass

    print("\n[export] 下一步: 用 STM32Cube.AI 导入此 ONNX 文件生成 C 代码,")
    print("         生成的 bearing_cnn.c/.h 放入 X-CUBE-AI/App/ 即可替换现有部署。",
          flush=True)


if __name__ == "__main__":
    main()
