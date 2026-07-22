# 数据集导入说明

本工程已经直接包含实际图片和标注：

```text
datasets/helmet_dataset/images
datasets/helmet_dataset/labels
datasets/helmet_dataset/helmet.yaml
```

## 导入到本工程

在 `rv1126_detect_helmet` 目录执行：

```bash
python tools/import_dataset.py
```

默认命令只验证内置数据集是否完整，不会复制或删除文件。

## 手动指定数据集路径

需要用另一份数据集替换内置数据时，可以手动指定：

```bash
python tools/import_dataset.py --source-dir D:\datasets\helmet_dataset --overwrite
```

导入完成后，再训练：

```bash
python tools/train_yolo26.py
```
