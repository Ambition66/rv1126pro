# 单目录复现说明

`rv1126_detect_helmet` 已整理为单目录工程，不再依赖同级的
`yolo26_helmet`、`rv1126_AI`、`rv1126_ffmpeg` 或 `yolov8` 目录。

## 已包含的文件

```text
datasets/helmet_dataset/       训练图片、YOLO 标注和数据集 YAML
models/yolo26n.pt              YOLO26n 初始权重
models/best.pt                 已训练头盔模型
models/helmet.onnx             已导出的 ONNX
third_party/yolo26/            本项目使用的定制 Ultralytics/YOLO26 源码
third_party/rv1126/include/     RKNN、RKMedia、FFmpeg 等头文件
third_party/rv1126/lib/         RV1126 ARM32 预编译动态库
```

## 仍需安装的系统工具

以下内容与机器、操作系统或 Rockchip 授权工具绑定，因此不属于项目文件依赖：

1. Python 3.8+ 和训练所需的 CUDA/PyTorch 运行环境。
2. 老 RV1126 使用的 RKNN-Toolkit 1.x。建议使用与板端 Runtime 匹配的 1.7.x 环境。
3. RV1126 SDK 中的 `arm-linux-gnueabihf-g++` 交叉编译器。
4. 真实 RV1126 板卡、摄像头驱动及 `/dev` 设备节点。

## 从零复现

复制目录后可先执行完整性检查：

```bash
python tools/check_self_contained.py
```

### 1. 创建训练环境

在工程根目录执行：

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-training.txt
```

Windows 激活命令为：

```powershell
.venv\Scripts\Activate.ps1
```

### 2. 验证内置数据集

```bash
python tools/import_dataset.py
```

需要替换为其他数据集时才使用：

```bash
python tools/import_dataset.py --source-yaml /path/to/helmet.yaml --source-dir /path/to/dataset --overwrite
```

### 3. 重新训练与导出

```bash
python tools/train_yolo26.py
python tools/export_onnx.py
python tools/make_quant_list.py
```

仓库已带有 `models/best.pt` 和 `models/helmet.onnx`，只验证部署时可以跳过训练和导出。

### 4. 转换 RKNN

在安装了老 RKNN-Toolkit 的转换环境中执行：

```bash
python tools/convert_rknn.py
```

产物为 `models/helmet.rknn`。RKNN-Toolkit 本身必须根据 Rockchip 的发行方式另行安装。

### 5. 交叉编译

确保交叉编译器在 `PATH` 中：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1
```

或显式指定：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1 \
  G++=/path/to/arm-linux-gnueabihf-g++
```

头文件和链接库全部从工程内的 `third_party/rv1126` 读取。

### 6. 板端部署

至少复制：

```text
rv1126_detect_helmet_app
models/helmet.rknn
models/labels.txt
```

如果板端系统没有相同版本的动态库，还需从 `third_party/rv1126/lib` 复制相应 `.so`，
并通过 `LD_LIBRARY_PATH` 指向它们。优先使用板端 SDK 自带的驱动配套库，尤其是
RKNN Runtime、RGA、MPP 和 ISP/AIQ 库，避免用户态库与内核驱动版本不匹配。
