"""训练入口 —— 最小可复现训练流程。

用法:
    python train.py                          # auto 数据源, 默认超参
    python train.py --source synthetic       # 用合成数据快速验证闭环
    python train.py --source cwru --epochs 50

输出 (runs/):
    best_model.pt        最佳验证精度权重
    history.json         每 epoch loss/acc
    confusion_matrix.png 测试集混淆矩阵
    training_curves.png  loss/acc 曲线
    report.txt           分类报告 + 资源占用
"""
import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

import config as C
from data_prep import build_dataset, to_model_input
from model import BearingCNN, count_parameters
from utils import set_seed, plot_confusion_matrix, plot_training_curves

# tqdm 可选
try:
    from tqdm import tqdm
except ImportError:
    def tqdm(x, **kw):
        return x


def split_dataset(X, y, val_ratio=C.VAL_SPLIT, test_ratio=C.TEST_SPLIT,
                  seed=C.RANDOM_SEED):
    """训练/验证/测试划分。优先 sklearn,缺失则手写分层划分。"""
    try:
        from sklearn.model_selection import train_test_split
        X_tr, X_te, y_tr, y_te = train_test_split(
            X, y, test_size=test_ratio, random_state=seed, stratify=y)
        X_tr, X_va, y_tr, y_va = train_test_split(
            X_tr, y_tr, test_size=val_ratio / (1 - test_ratio),
            random_state=seed, stratify=y_tr)
        return (X_tr, y_tr), (X_va, y_va), (X_te, y_te)
    except ImportError:
        rng = np.random.default_rng(seed)
        idx = rng.permutation(len(y))
        n = len(y)
        n_te = int(n * test_ratio)
        n_va = int(n * val_ratio)
        te, va, tr = idx[:n_te], idx[n_te:n_te + n_va], idx[n_te + n_va:]
        return (X[tr], y[tr]), (X[va], y[va]), (X[te], y[te])


def evaluate(model, loader, device, criterion):
    model.eval()
    total_loss, correct, total = 0.0, 0, 0
    all_pred, all_true = [], []
    with torch.no_grad():
        for xb, yb in loader:
            xb, yb = xb.to(device), yb.to(device)
            out = model(xb)
            loss = criterion(out, yb)
            total_loss += loss.item() * xb.size(0)
            pred = out.argmax(1)
            correct += (pred == yb).sum().item()
            total += xb.size(0)
            all_pred.append(pred.cpu().numpy())
            all_true.append(yb.cpu().numpy())
    avg_loss = total_loss / max(total, 1)
    acc = correct / max(total, 1)
    return avg_loss, acc, np.concatenate(all_true), np.concatenate(all_pred)


def main():
    parser = argparse.ArgumentParser(description="1D-CNN 轴承故障检测训练")
    parser.add_argument("--source", default="auto",
                        choices=["auto", "cwru", "synthetic"],
                        help="数据源")
    parser.add_argument("--epochs", type=int, default=C.EPOCHS)
    parser.add_argument("--batch-size", type=int, default=C.BATCH_SIZE)
    parser.add_argument("--lr", type=float, default=C.LR)
    parser.add_argument("--run-name", default=None, help="输出子目录名")
    args = parser.parse_args()

    set_seed(C.RANDOM_SEED)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    run_name = args.run_name or time.strftime("run_%Y%m%d_%H%M%S")
    out_dir = C.RUNS_DIR / run_name
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"[train] 输出目录: {out_dir}", flush=True)
    print(f"[train] 设备: {device}", flush=True)

    # 1. 数据
    X, y = build_dataset(args.source)
    X = to_model_input(X)                     # (N, 1, 1024)
    (X_tr, y_tr), (X_va, y_va), (X_te, y_te) = split_dataset(X, y)
    print(f"[train] train={len(y_tr)} val={len(y_va)} test={len(y_te)}",
          flush=True)

    def make_loader(Xa, ya, shuffle):
        ds = TensorDataset(torch.from_numpy(Xa), torch.from_numpy(ya))
        return DataLoader(ds, batch_size=args.batch_size, shuffle=shuffle)

    tr_loader = make_loader(X_tr, y_tr, True)
    va_loader = make_loader(X_va, y_va, False)
    te_loader = make_loader(X_te, y_te, False)

    # 2. 模型
    model = BearingCNN().to(device)
    n_params = count_parameters(model)
    print(f"[train] 模型参数量: {n_params} ({n_params * 4 / 1024:.1f} KB)",
          flush=True)
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr,
                                 weight_decay=C.WEIGHT_DECAY)

    # 3. 训练循环
    history = {"train_loss": [], "val_loss": [], "train_acc": [], "val_acc": []}
    best_val_acc = 0.0
    best_path = out_dir / "best_model.pt"
    for epoch in range(1, args.epochs + 1):
        model.train()
        t0 = time.time()
        tr_loss, tr_correct, tr_total = 0.0, 0, 0
        for xb, yb in tqdm(tr_loader, desc=f"Epoch {epoch}/{args.epochs}",
                          leave=False):
            xb, yb = xb.to(device), yb.to(device)
            optimizer.zero_grad()
            out = model(xb)
            loss = criterion(out, yb)
            loss.backward()
            optimizer.step()
            tr_loss += loss.item() * xb.size(0)
            tr_correct += (out.argmax(1) == yb).sum().item()
            tr_total += xb.size(0)
        tr_loss /= max(tr_total, 1)
        tr_acc = tr_correct / max(tr_total, 1)
        va_loss, va_acc, _, _ = evaluate(model, va_loader, device, criterion)

        history["train_loss"].append(tr_loss)
        history["val_loss"].append(va_loss)
        history["train_acc"].append(tr_acc)
        history["val_acc"].append(va_acc)
        print(f"  Epoch {epoch:3d} | "
              f"train_loss={tr_loss:.4f} train_acc={tr_acc:.4f} | "
              f"val_loss={va_loss:.4f} val_acc={va_acc:.4f} | "
              f"{time.time() - t0:.1f}s", flush=True)

        if va_acc > best_val_acc:
            best_val_acc = va_acc
            torch.save({"model_state": model.state_dict(),
                        "val_acc": va_acc,
                        "epoch": epoch,
                        "config": {k: getattr(C, k) for k in
                                   ["SAMPLE_LEN", "NUM_CLASSES", "CLASS_NAMES"]}},
                       best_path)
            print(f"    -> saved best (val_acc={va_acc:.4f})", flush=True)

    # 4. 保存曲线
    with open(out_dir / "history.json", "w") as f:
        json.dump(history, f, indent=2)
    plot_training_curves(history, out_dir / "training_curves.png")

    # 5. 测试集评估
    print("[train] 在测试集上评估最佳模型...", flush=True)
    ckpt = torch.load(best_path, map_location=device, weights_only=False)
    model.load_state_dict(ckpt["model_state"])
    te_loss, te_acc, y_true, y_pred = evaluate(model, te_loader, device, criterion)

    # 混淆矩阵 + 报告
    try:
        from sklearn.metrics import confusion_matrix, classification_report
        cm = confusion_matrix(y_true, y_pred)
        plot_confusion_matrix(cm, C.CLASS_NAMES, out_dir / "confusion_matrix.png")
        report = classification_report(y_true, y_pred, target_names=C.CLASS_NAMES, zero_division=0)
    except ImportError:
        cm = np.zeros((C.NUM_CLASSES, C.NUM_CLASSES), dtype=int)
        for t, p in zip(y_true, y_pred):
            cm[t, p] += 1
        plot_confusion_matrix(cm, C.CLASS_NAMES, out_dir / "confusion_matrix.png")
        report = f"(sklearn 未安装) 混淆矩阵:\n{cm}\n测试精度: {te_acc:.4f}\n"

    with open(out_dir / "report.txt", "w") as f:
        f.write(f"数据源: {args.source}\n")
        f.write(f"参数量: {n_params} ({n_params * 4 / 1024:.1f} KB)\n")
        f.write(f"最佳验证精度: {best_val_acc:.4f}\n")
        f.write(f"测试精度: {te_acc:.4f}  测试 loss: {te_loss:.4f}\n\n")
        f.write("分类报告:\n")
        f.write(report)

    print(f"\n[train] 完成! 测试精度 = {te_acc:.4f}", flush=True)
    print(f"[train] 结果保存在: {out_dir}", flush=True)
    print(f"         - best_model.pt / confusion_matrix.png / "
          f"training_curves.png / report.txt", flush=True)


if __name__ == "__main__":
    main()
