#!/usr/bin/env python3
"""Train a YOLO26 helmet detector and normalize the output as models/best.pt.

This script follows the training style used by ../yolo26_helmet/train.py, but
keeps paths relative to rv1126_detect_helmet so the model pipeline can be
reproduced from this directory.

Example:
    python tools/train_yolo26.py

    python tools/train_yolo26.py --model yolo26n.pt --data datasets/helmet_dataset/helmet.yaml
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA = PROJECT_ROOT / "datasets" / "helmet_dataset" / "helmet.yaml"
DEFAULT_EXPORT = PROJECT_ROOT / "models" / "best.pt"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train YOLO26 helmet detector.")
    parser.add_argument(
        "--model",
        default="yolo26n.pt",
        help="Initial model weights or YAML, e.g. yolo26n.pt.",
    )
    parser.add_argument(
        "--data",
        type=Path,
        default=DEFAULT_DATA,
        help="Dataset YAML path. Default: datasets/helmet_dataset/helmet.yaml",
    )
    parser.add_argument("--epochs", type=int, default=300, help="Training epochs.")
    parser.add_argument("--imgsz", type=int, default=640, help="Training image size.")
    parser.add_argument(
        "--batch",
        default="-1",
        help="Batch size. Use -1 for Ultralytics AutoBatch.",
    )
    parser.add_argument("--workers", type=int, default=4, help="Data loader workers.")
    parser.add_argument("--patience", type=int, default=50, help="Early stopping patience.")
    parser.add_argument(
        "--device",
        default="0",
        help="Training device. Use 0 for first GPU, cpu for CPU.",
    )
    parser.add_argument(
        "--project",
        type=Path,
        default=PROJECT_ROOT / "runs" / "train",
        help="Ultralytics output project directory.",
    )
    parser.add_argument("--name", default="helmet_yolo26n", help="Training run name.")
    parser.add_argument(
        "--export-best",
        type=Path,
        default=DEFAULT_EXPORT,
        help="Copy trained best.pt to this path. Default: models/best.pt",
    )
    parser.add_argument(
        "--no-copy",
        action="store_true",
        help="Do not copy run weights/best.pt to models/best.pt.",
    )
    return parser.parse_args()


def parse_batch(value: str) -> int | float:
    if "." in value:
        return float(value)
    return int(value)


def main() -> int:
    args = parse_args()
    data = args.data if args.data.is_absolute() else PROJECT_ROOT / args.data
    project = args.project if args.project.is_absolute() else PROJECT_ROOT / args.project
    export_best = args.export_best if args.export_best.is_absolute() else PROJECT_ROOT / args.export_best

    if not data.is_file():
        raise FileNotFoundError(f"dataset yaml not found: {data}")

    try:
        from ultralytics import YOLO
    except ImportError as exc:
        raise RuntimeError(
            "ultralytics is not installed. Run this script in the YOLO26 training environment."
        ) from exc

    model = YOLO(args.model)
    results = model.train(
        data=str(data),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=parse_batch(str(args.batch)),
        workers=args.workers,
        patience=args.patience,
        device=args.device,
        task="detect",
        mode="train",
        verbose=True,
        project=str(project),
        name=args.name,
    )

    run_dir = Path(getattr(results, "save_dir", project / args.name))
    trained_best = run_dir / "weights" / "best.pt"
    if not trained_best.is_file():
        raise FileNotFoundError(f"trained best.pt not found: {trained_best}")

    if not args.no_copy:
        export_best.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(trained_best, export_best)
        print(f"best.pt copied to: {export_best}")

    print(f"training run: {run_dir}")
    print(f"trained weights: {trained_best}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
