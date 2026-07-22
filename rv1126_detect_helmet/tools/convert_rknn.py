#!/usr/bin/env python3
"""Convert helmet.onnx to helmet.rknn for RV1126.

Run this on the model conversion machine with the old RKNN-Toolkit installed.
The generated RKNN should be copied to models/helmet.rknn on the RV1126 board.
"""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert ONNX helmet model to RKNN")
    parser.add_argument("--onnx", default="models/helmet.onnx", help="Input ONNX path")
    parser.add_argument("--output", default="models/helmet.rknn", help="Output RKNN path")
    parser.add_argument(
        "--dataset",
        default="datasets/helmet_dataset/rknn_quant.txt",
        help="Calibration image list for quantization",
    )
    parser.add_argument("--target", default="rv1126", help="RKNN target platform")
    parser.add_argument("--mean", default="0,0,0", help="Mean values, comma separated")
    parser.add_argument("--std", default="255,255,255", help="Std values, comma separated")
    parser.add_argument(
        "--outputs",
        default="",
        help="Optional ONNX output names, comma separated. Useful for YOLO multi-output export.",
    )
    parser.add_argument(
        "--toolkit",
        choices=("rv1126", "toolkit2"),
        default="rv1126",
        help="Use old RKNN-Toolkit style config for RV1126 by default.",
    )
    parser.add_argument("--no-quant", action="store_true", help="Disable int8 quantization")
    return parser.parse_args()


def parse_float_list(value: str) -> list[float]:
    return [float(item.strip()) for item in value.split(",") if item.strip()]


def parse_str_list(value: str) -> list[str] | None:
    items = [item.strip() for item in value.split(",") if item.strip()]
    return items or None


def main() -> int:
    args = parse_args()

    try:
        from rknn.api import RKNN
    except ImportError as exc:
        raise SystemExit(
            "rknn-toolkit is not installed. Install Rockchip rknn-toolkit on the conversion machine first."
        ) from exc

    onnx_path = Path(args.onnx)
    output_path = Path(args.output)
    dataset_path = Path(args.dataset)

    if not onnx_path.exists():
        raise SystemExit(f"ONNX file not found: {onnx_path}")
    if not args.no_quant and not dataset_path.exists():
        raise SystemExit(f"Quantization dataset list not found: {dataset_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    rknn = RKNN(verbose=True)

    config_kwargs = {
        "mean_values": [parse_float_list(args.mean)],
        "std_values": [parse_float_list(args.std)],
        "target_platform": args.target,
    }
    if args.toolkit == "rv1126":
        config_kwargs.update(
            {
                "reorder_channel": "0 1 2",
                "optimization_level": 3,
                "output_optimize": 1,
                "quantize_input_node": not args.no_quant,
                "force_builtin_perm": True,
            }
        )
    else:
        config_kwargs["quantized_dtype"] = "asymmetric_quantized-u8"

    ret = rknn.config(**config_kwargs)
    if ret not in (0, None):
        raise SystemExit(f"rknn.config failed: {ret}")

    outputs = parse_str_list(args.outputs)
    if outputs:
        ret = rknn.load_onnx(model=str(onnx_path), outputs=outputs)
    else:
        ret = rknn.load_onnx(model=str(onnx_path))
    if ret != 0:
        raise SystemExit(f"rknn.load_onnx failed: {ret}")

    ret = rknn.build(
        do_quantization=not args.no_quant,
        dataset=None if args.no_quant else str(dataset_path),
    )
    if ret != 0:
        raise SystemExit(f"rknn.build failed: {ret}")

    ret = rknn.export_rknn(str(output_path))
    if ret != 0:
        raise SystemExit(f"rknn.export_rknn failed: {ret}")

    rknn.release()
    print(f"RKNN exported: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
