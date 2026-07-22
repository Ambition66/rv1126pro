# 数据集导入说明

`yolo26_helmet` 目录下目前没有实际图片和标注数据集；它提供的是训练脚本和 `helmet.yaml`。

`yolo26_helmet/helmet.yaml` 中的数据集根目录是：

```text
E:\YOLOV26\ultralytics-main\datasets\helmet_dataset
```

当前这台工作区机器没有 E 盘，所以这里不能直接读取那份数据。请在拥有该数据集的训练机上执行导入。

## 导入到本工程

在 `rv1126_detect_helmet` 目录执行：

```bash
python tools/import_dataset.py
```

脚本会读取：

```text
../yolo26_helmet/helmet.yaml
```

然后复制：

```text
images/
labels/
```

到：

```text
datasets/helmet_dataset/
```

并生成本工程使用的：

```text
datasets/helmet_dataset/helmet.yaml
```

## 手动指定数据集路径

如果原始数据集不在 `yolo26_helmet/helmet.yaml` 写的路径，可以手动指定：

```bash
python tools/import_dataset.py --source-dir D:\datasets\helmet_dataset --overwrite
```

导入完成后，再训练：

```bash
python tools/train_yolo26.py
```
