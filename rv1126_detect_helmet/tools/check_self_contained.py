#!/usr/bin/env python3
"""Verify that the project-local assets required for reproduction are present."""

from __future__ import annotations

import hashlib
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp"}
KEY_HASHES = {
    "models/yolo26n.pt": "9B09CC8BF347F0FC8A5F7657480587F25DB09B34BF33B0652110FB03A8AD4FEF",
    "models/best.pt": "0289F4858FD004ABA916BD651E4AD81C2151E500AF76253BF8A278742FBC1B21",
    "models/helmet.onnx": "35687C94A2082B378AF257C4B38D5860AC8D7B97F2F34BAE3089487511D8D4FD",
    "third_party/rv1126/lib/platform/librknn_api.so": "4D3AAE409B6F16B3552A3DD307267C73B5557770E7FA34BC5C5BF1BB3271BAF2",
    "third_party/rv1126/lib/platform/librknn_runtime.so": "3BF429DF141AEF403B088ADDBC39E0A14440254CE119C5431D4E49CB0CAB78DB",
    "third_party/rv1126/lib/platform/libeasymedia.so": "0313BB19105152FC7E39107EA712676AB86599FB6993E3ABC6F1E3E373D4F5D3",
    "third_party/rv1126/lib/ffmpeg/libavformat.so": "9729D9A50D5A08939E5E7BEFB2B6596EFD495CD0B7F056B4A4EF573867861EAA",
}
REQUIRED_PATHS = (
    "datasets/helmet_dataset/helmet.yaml",
    "third_party/yolo26/LICENSE",
    "third_party/yolo26/pyproject.toml",
    "third_party/yolo26/ultralytics/__init__.py",
    "third_party/rv1126/include/rknn/rknn_api.h",
    "third_party/rv1126/include/rkmedia/rkmedia_api.h",
    "third_party/rv1126/include/ffmpeg/libavformat/avformat.h",
    "third_party/rv1126/lib/platform/librknn_api.so",
    "third_party/rv1126/lib/platform/librknn_runtime.so",
    "third_party/rv1126/lib/platform/libeasymedia.so",
    "third_party/rv1126/lib/ffmpeg/libavformat.so",
    "third_party/rv1126/lib/srt/libsrt.so",
    "third_party/rv1126/lib/openssl/libssl.so",
    "third_party/rv1126/lib/x264/libx264.so",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def verify_dataset() -> tuple[int, int]:
    dataset = PROJECT_ROOT / "datasets" / "helmet_dataset"
    images = [
        path
        for path in (dataset / "images").rglob("*")
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    ]
    missing_labels = []
    annotation_count = 0
    for image in images:
        relative = image.relative_to(dataset / "images").with_suffix(".txt")
        label = dataset / "labels" / relative
        if not label.is_file():
            missing_labels.append(str(relative))
            continue
        for line_number, line in enumerate(label.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            fields = line.split()
            if len(fields) != 5 or int(fields[0]) not in (0, 1):
                raise RuntimeError(f"invalid YOLO label: {label}:{line_number}")
            values = [float(value) for value in fields[1:]]
            if any(value < 0.0 or value > 1.0 for value in values):
                raise RuntimeError(f"out-of-range YOLO label: {label}:{line_number}")
            annotation_count += 1
    if missing_labels:
        raise RuntimeError(f"{len(missing_labels)} images have no label, first: {missing_labels[0]}")
    return len(images), annotation_count


def main() -> int:
    missing = [path for path in REQUIRED_PATHS if not (PROJECT_ROOT / path).exists()]
    if missing:
        raise SystemExit("missing required assets:\n  " + "\n  ".join(missing))

    for relative, expected in KEY_HASHES.items():
        actual = sha256(PROJECT_ROOT / relative)
        if actual != expected:
            raise SystemExit(f"asset hash mismatch: {relative}\nexpected={expected}\nactual={actual}")

    image_count, annotation_count = verify_dataset()
    print(f"self-contained check passed: images={image_count} annotations={annotation_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
