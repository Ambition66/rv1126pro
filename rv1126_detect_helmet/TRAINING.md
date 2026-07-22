# 训练生成 best.pt

本工程使用 `tools/train_yolo26.py` 训练 YOLO26 头盔检测模型，并将训练产物统一复制为：

```text
models/best.pt
```

该文件是后续 ONNX 导出的默认输入。

## 1. 数据集准备

数据集已经包含在本工程中，可先执行完整性检查：

```bash
python tools/import_dataset.py
```

如果数据集放在其他位置：

```bash
python tools/import_dataset.py --source-dir D:\datasets\helmet_dataset --overwrite
```

导入后，本工程的数据集目录为：

目录结构：

```text
datasets/helmet_dataset/
  helmet.yaml
  images/
    train/
    val/
  labels/
    train/
    val/
```

每张图片对应一个同名 YOLO 标注文件：

```text
images/train/000001.jpg
labels/train/000001.txt
```

标注格式：

```text
class_id x_center y_center width height
```

坐标为 0-1 归一化值。

当前 `helmet.yaml` 默认沿用 `yolo26_helmet` 的两类方案：

```yaml
nc: 2
names: ["helmet", "no_helmet"]
```

产品版本建议后续扩展为：

```yaml
nc: 3
names: ["rider", "helmet", "no_helmet"]
```

这样更容易判断“未戴头盔的人是否为电动车驾驶员”。

## 2. 安装训练环境

训练应在 PC 上执行，建议使用带 NVIDIA GPU 的 Linux/Windows 环境。

本工程把定制 YOLO26 源码放在 `third_party/yolo26`。安装训练依赖：

```bash
python -m pip install -r requirements-training.txt
```

训练脚本默认直接加载这份本地源码，仍可通过 `--ultralytics-dir` 指定另一份实现。

## 3. 开始训练

在 `rv1126_detect_helmet` 目录执行：

```bash
python tools/train_yolo26.py
```

默认参数参考 `yolo26_helmet/train.py`：

```text
model=yolo26n.pt
data=datasets/helmet_dataset/helmet.yaml
epochs=300
imgsz=640
batch=-1
workers=4
patience=50
device=0
```

训练输出目录：

```text
runs/train/helmet_yolo26n/
```

训练完成后脚本会自动复制：

```text
runs/train/helmet_yolo26n/weights/best.pt
```

到：

```text
models/best.pt
```

## 4. 常用参数

指定初始模型：

```bash
python tools/train_yolo26.py --model yolo26n.pt
```

指定数据集：

```bash
python tools/train_yolo26.py --data datasets/helmet_dataset/helmet.yaml
```

CPU 训练：

```bash
python tools/train_yolo26.py --device cpu --batch 4
```

手动指定 batch：

```bash
python tools/train_yolo26.py --batch 16
```

只保留训练目录，不复制到 `models/best.pt`：

```bash
python tools/train_yolo26.py --no-copy
```

## 5. 下一步

生成 `models/best.pt` 后，导出 ONNX：

```bash
python tools/export_onnx.py --backend ultralytics --weights models/best.pt
```

输出：

```text
models/helmet.onnx
```
