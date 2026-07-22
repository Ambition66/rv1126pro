# ONNX 导出说明

本目录提供 `tools/export_onnx.py` 作为统一 ONNX 导出入口。默认输入为：

```text
models/best.pt
```

默认输出为：

```text
models/helmet.onnx
```

注意：ONNX 导出应在 PC 训练/导出环境执行，需要安装 Python、PyTorch 以及对应的 YOLO/Ultralytics 依赖；不要在 RV1126 板端执行导出。

## 1. YOLO26/Ultralytics 导出

适用于 `yolo26_helmet` 训练得到的 `best.pt`，也适用于 Ultralytics YOLOv8/YOLO11 等模型。

```bash
python tools/export_onnx.py --backend ultralytics --weights models/best.pt
```

默认导出参数：

```text
imgsz=640
batch=1
opset=12
output=models/helmet.onnx
```

如果要导出 YOLO26 one-to-many head，也就是需要板端 NMS 后处理的传统输出：

```bash
python tools/export_onnx.py --backend ultralytics --weights models/best.pt --end2end-false
```

如果环境安装了 onnxsim，并希望导出时尝试简化：

```bash
python tools/export_onnx.py --backend ultralytics --weights models/best.pt --simplify
```

## 2. YOLOv5 导出

适用于第一版推荐的 YOLOv5n/YOLOv5s 头盔模型。需要本机有一个 YOLOv5 仓库，并传入仓库路径：

```bash
python tools/export_onnx.py ^
  --backend yolov5 ^
  --weights models/best.pt ^
  --yolov5-dir C:\path\to\yolov5 ^
  --simplify
```

Linux 写法：

```bash
python tools/export_onnx.py \
  --backend yolov5 \
  --weights models/best.pt \
  --yolov5-dir /path/to/yolov5 \
  --simplify
```

## 3. 导出后检查

导出成功后应存在：

```text
models/helmet.onnx
```

建议先用训练环境做一次 ONNX 推理检查：

```bash
yolo predict model=models/helmet.onnx source=path/to/test.jpg
```

如果使用 YOLOv5，也可以在 YOLOv5 环境中用 ONNXRuntime 或 `detect.py` 检查。

## 4. 下一步

ONNX 验证通过后，再进行 RKNN 转换：

```text
models/helmet.onnx -> models/helmet.rknn
```

转换脚本后续放在：

```text
tools/convert_onnx_to_rknn.py
```
