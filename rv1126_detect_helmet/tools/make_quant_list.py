#!/usr/bin/env python3
"""Create an RKNN quantization image list from the local helmet dataset."""

from __future__ import annotations

import argparse
from pathlib import Path


IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate RKNN quantization image list")
    parser.add_argument(
        "--images",
        default="datasets/helmet_dataset/images/train",
        help="Image directory used for calibration",
    )
    parser.add_argument(
        "--output",
        default="datasets/helmet_dataset/rknn_quant.txt",
        help="Output text file",
    )
    parser.add_argument("--limit", type=int, default=200, help="Max image count")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    image_dir = Path(args.images)
    output_path = Path(args.output)

    if not image_dir.exists():
        raise SystemExit(f"image directory not found: {image_dir}")

    images = [
        path
        for path in sorted(image_dir.rglob("*"))
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    ]
    if args.limit > 0:
        images = images[: args.limit]
    if not images:
        raise SystemExit(f"no calibration images found in: {image_dir}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as fp:
        for image in images:
            fp.write(str(image.as_posix()))
            fp.write("\n")

    print(f"wrote {len(images)} images to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
