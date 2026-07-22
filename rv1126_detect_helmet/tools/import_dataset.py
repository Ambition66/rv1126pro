#!/usr/bin/env python3
"""Validate or optionally replace the dataset vendored with this project."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEST = PROJECT_ROOT / "datasets" / "helmet_dataset"
DEFAULT_SOURCE_YAML = DEFAULT_DEST / "helmet.yaml"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Import helmet dataset into this project.")
    parser.add_argument(
        "--source-yaml",
        type=Path,
        default=DEFAULT_SOURCE_YAML,
        help="Dataset YAML to read. Default: the vendored dataset YAML",
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
        if not source_dir.is_absolute():
            project_relative = PROJECT_ROOT / source_dir
            yaml_relative = source_yaml.parent / source_dir
            source_dir = project_relative if project_relative.is_dir() else yaml_relative
    else:
        source_dir = args.source_dir
    source_dir = source_dir.resolve()

    if not source_dir.is_dir():
        raise FileNotFoundError(
            f"dataset source directory not found: {source_dir}\n"
            "Pass --source-dir explicitly when importing an external dataset."
        )

    if source_dir == dest_dir:
        for child in ("images", "labels"):
            if not (dest_dir / child).is_dir():
                raise FileNotFoundError(f"vendored dataset child not found: {dest_dir / child}")
        print(f"vendored dataset is ready: {dest_dir}")
        return 0

    if dest_dir.exists() and args.overwrite:
        shutil.rmtree(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)

    copy_child(source_dir, dest_dir, "images", args.overwrite)
    copy_child(source_dir, dest_dir, "labels", args.overwrite)

    local_yaml = dest_dir / "helmet.yaml"
    yaml_text = source_yaml.read_text(encoding="utf-8")
    yaml_text = "\n".join(
        "path: ./datasets/helmet_dataset" if line.strip().startswith("path:") else line
        for line in yaml_text.splitlines()
    )
    local_yaml.write_text(yaml_text + "\n", encoding="utf-8")
    print(f"wrote local dataset yaml: {local_yaml}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
