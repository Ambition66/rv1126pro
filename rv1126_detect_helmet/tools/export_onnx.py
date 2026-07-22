#!/usr/bin/env python3
"""Export a trained helmet detection model to ONNX.

This script keeps ONNX generation reproducible for the RV1126 helmet project.
It supports two routes:

1. ultralytics: for YOLO26/YOLOv8 style models loaded with ultralytics.YOLO.
2. yolov5: call a local YOLOv5 repository's export.py.

Examples:
    python tools/export_onnx.py --backend ultralytics --weights models/best.pt

    python tools/export_onnx.py --backend ultralytics --weights models/best.pt --end2end-false

    python tools/export_onnx.py --backend yolov5 --weights models/best.pt --yolov5-dir ../yolov5
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WEIGHTS = PROJECT_ROOT / "models" / "best.pt"
DEFAULT_OUTPUT = PROJECT_ROOT / "models" / "helmet.onnx"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export helmet detection weights to ONNX.")
    parser.add_argument(
        "--backend",
        choices=("ultralytics", "yolov5"),
        default="ultralytics",
        help="Export backend. Use ultralytics for YOLO26, yolov5 for a YOLOv5 repo.",
    )
    parser.add_argument(
        "--weights",
        type=Path,
        default=DEFAULT_WEIGHTS,
        help="Path to trained .pt weights. Default: models/best.pt",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="Output ONNX path. Default: models/helmet.onnx",
    )
    parser.add_argument("--imgsz", type=int, default=640, help="Export image size.")
    parser.add_argument("--batch", type=int, default=1, help="Export batch size.")
    parser.add_argument("--opset", type=int, default=12, help="ONNX opset for YOLOv5 export.")
    parser.add_argument(
        "--simplify",
        action="store_true",
        help="Request simplified ONNX when the backend supports it.",
    )
    parser.add_argument(
        "--end2end-false",
        action="store_true",
        help="For YOLO26/Ultralytics, export the one-to-many head that requires NMS.",
    )
    parser.add_argument(
        "--yolov5-dir",
        type=Path,
        default=None,
        help="Path to a local YOLOv5 repo containing export.py. Required for --backend yolov5.",
    )
    return parser.parse_args()


def ensure_weights(weights: Path) -> Path:
    weights = weights.resolve()
    if not weights.is_file():
        raise FileNotFoundError(f"weights not found: {weights}")
    return weights


def normalize_output(src_onnx: Path, dst_onnx: Path) -> Path:
    src_onnx = src_onnx.resolve()
    dst_onnx = dst_onnx.resolve()
    dst_onnx.parent.mkdir(parents=True, exist_ok=True)

    if not src_onnx.is_file():
        raise FileNotFoundError(f"exported ONNX not found: {src_onnx}")

    if src_onnx != dst_onnx:
        shutil.copy2(src_onnx, dst_onnx)

    return dst_onnx


def export_with_ultralytics(args: argparse.Namespace) -> Path:
    try:
        from ultralytics import YOLO
    except ImportError as exc:
        raise RuntimeError(
            "ultralytics is not installed. Install it in the training/export environment first."
        ) from exc

    weights = ensure_weights(args.weights)
    model = YOLO(str(weights))

    export_kwargs = {
        "format": "onnx",
        "imgsz": args.imgsz,
        "batch": args.batch,
        "opset": args.opset,
        "simplify": args.simplify,
    }
    if args.end2end_false:
        export_kwargs["end2end"] = False

    exported = model.export(**export_kwargs)
    exported_path = Path(exported)
    if not exported_path.is_absolute():
        exported_path = (Path.cwd() / exported_path).resolve()

    return normalize_output(exported_path, args.output)


def export_with_yolov5(args: argparse.Namespace) -> Path:
    weights = ensure_weights(args.weights)
    if args.yolov5_dir is None:
        raise ValueError("--yolov5-dir is required when --backend yolov5")

    yolov5_dir = args.yolov5_dir.resolve()
    export_py = yolov5_dir / "export.py"
    if not export_py.is_file():
        raise FileNotFoundError(f"YOLOv5 export.py not found: {export_py}")

    cmd = [
        "python",
        str(export_py),
        "--weights",
        str(weights),
        "--img",
        str(args.imgsz),
        "--batch",
        str(args.batch),
        "--include",
        "onnx",
        "--opset",
        str(args.opset),
    ]
    if args.simplify:
        cmd.append("--simplify")

    subprocess.run(cmd, cwd=str(yolov5_dir), check=True)

    exported_path = weights.with_suffix(".onnx")
    return normalize_output(exported_path, args.output)


def main() -> int:
    args = parse_args()
    args.output = (PROJECT_ROOT / args.output).resolve() if not args.output.is_absolute() else args.output

    if args.backend == "ultralytics":
        output = export_with_ultralytics(args)
    else:
        output = export_with_yolov5(args)

    print(f"ONNX exported: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
