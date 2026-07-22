# 内置第三方依赖

## yolo26

`yolo26/` 是本项目训练和 ONNX 导出所使用的定制 Ultralytics/YOLO26 源码，
从原工作区的 `yolo26_helmet` 收拢而来。其许可证见 `yolo26/LICENSE`（AGPL-3.0）。

## rv1126

`rv1126/include/` 和 `rv1126/lib/` 从原 RV1126 SDK 示例工程收拢，目标 ABI 为：

```text
ARM 32-bit hard-float
老 RV1126/RV1109 RKNPU
旧版 RKNN API（非 RKNPU2/RV1126B）
```

目录含 RKNN、RKMedia、RGA、MPP、DRM、ISP/AIQ、FFmpeg、SRT、OpenSSL、x264、
ALSA 和 V4L2 等板端动态库。它们用于交叉链接或在匹配的板端系统上部署，不能在
x86/x64 主机上运行。实际发布时应遵守 Rockchip SDK 及各开源组件的许可证要求。

为避免驱动 ABI 不匹配，板端已有同版本库时优先使用系统库；只有缺失时才部署这里的 `.so`。
