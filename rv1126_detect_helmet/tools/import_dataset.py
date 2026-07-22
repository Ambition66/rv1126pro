#!/usr/bin/env python3
"""Import the helmet dataset referenced by yolo26_helmet/helmet.yaml.

The yolo26_helmet project contains the training entry and dataset YAML, but in
this workspace the actual dataset is referenced by an external path such as:

    E:\\YOLOV26\\ultralytics-main\\datasets\\helmet_dataset

Run this script on the machine where that dataset path exists. It copies the
dataset into rv1126_detect_helmet/datasets/helmet_dataset so this project can be
self-contained for training and export.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_YAML = PROJECT_ROOT.parent / "yolo26_helmet" / "helmet.yaml"
DEFAULT_DEST = PROJECT_ROOT / "datasets" / "helmet_dataset"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Import helmet dataset into this project.")
    parser.add_argument(
        "--source-yaml",
        type=Path,
        default=DEFAULT_SOURCE_YAML,
        help="Dataset YAML to read. Default: ../yolo26_helmet/helmet.yaml",
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=None,
        help="Override dataset root directory. If omitted, read the path field from --source-yaml.",
    )
    parser.add_argument(
        "--dest-dir",
        type=Path,
        default=DEFAULT_DEST,
        help="Destination dataset directory. Default: datasets/helmet_dataset",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Remove existing destination before copying.",
    )
    return parser.parse_args()


def read_simple_yaml_field(yaml_path: Path, field: str) -> str | None:
    for raw_line in yaml_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, value = line.split(":", 1)
        if key.strip() == field:
            return value.strip().strip("'\"")
    return None


def copy_child(src_root: Path, dst_root: Path, child: str, overwrite: bool) -> None:
    src = src_root / child
    dst = dst_root / child
    if not src.exists():
        raise FileNotFoundError(f"dataset child not found: {src}")
    if dst.exists() and overwrite:
        if dst.is_dir():
            shutil.rmtree(dst)
        else:
            dst.unlink()
    if dst.exists():
        print(f"skip existing: {dst}")
        return
    if src.is_dir():
        shutil.copytree(src, dst)
    else:
        shutil.copy2(src, dst)
    print(f"copied: {src} -> {dst}")


def main() -> int:
    args = parse_args()
    source_yaml = args.source_yaml.resolve()
    dest_dir = args.dest_dir.resolve()

    if not source_yaml.is_file():
        raise FileNotFoundError(f"source yaml not found: {source_yaml}")

    if args.source_dir is None:
        source_path = read_simple_yaml_field(source_yaml, "path")
        if not source_path:
            raise ValueError(f"missing path field in {source_yaml}")
        source_dir = Path(source_path)
    else:
        source_dir = args.source_dir
    source_dir = source_dir.resolve()

    if not source_dir.is_dir():
        raise FileNotFoundError(
            f"dataset source directory not found: {source_dir}\n"
            "Run this on the machine where the yolo26_helmet dataset path exists, "
            "or pass --source-dir explicitly."
        )

    if dest_dir.exists() and args.overwrite:
        shutil.rmtree(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)

    copy_child(source_dir, dest_dir, "images", args.overwrite)
    copy_child(source_dir, dest_dir, "labels", args.overwrite)

    local_yaml = dest_dir / "helmet.yaml"
    yaml_text = source_yaml.read_text(encoding="utf-8")
    yaml_text = "\n".join(
        "./datasets/helmet_dataset" if line.strip().startswith("path:") else line
        for line in yaml_text.splitlines()
    )
    local_yaml.write_text(yaml_text + "\n", encoding="utf-8")
    print(f"wrote local dataset yaml: {local_yaml}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
