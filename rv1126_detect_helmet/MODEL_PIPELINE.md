# 模型训练、转换、编译与部署流程

本工程包含两条相互独立的构建链：

1. 将 PyTorch 模型转换为 RV1126 NPU 使用的 RKNN 模型。
2. 将 C++ 业务代码交叉编译为 RV1126 板端程序。

最终模型不会编进可执行文件。板端程序启动后动态读取 `models/helmet.rknn`。

## 1. 完整产物演变

```text
通用 YOLO26n
models/yolo26n.pt
        ↓ 使用头盔数据集迁移训练
头盔 PyTorch 模型
models/best.pt
        ↓ ONNX 导出
六输出浮点计算图
models/helmet.onnx
        ↓ 使用 200 张校准图片进行 INT8 量化
RV1126 NPU 模型
models/helmet.rknn
        ↓ 板端 RKNN Runtime 加载
helmet / no_helmet 检测结果
```

## 2. 初始模型

初始权重为：

```text
models/yolo26n.pt
```

这是通用 YOLO26n 预训练权重，模型输入为 RGB `640x640`。`n` 表示 nano
轻量版本，适合 RV1126 这类资源受限设备。

训练和导出使用工程内置的定制 YOLO26 源码：

```text
third_party/yolo26/
```

## 3. 数据集

数据集位于：

```text
datasets/helmet_dataset/
```

当前包含 250 张可训练图片和 793 个目标标注，类别为：

```yaml
nc: 2
names: ["helmet", "no_helmet"]
```

类别编号：

```text
0 = helmet
1 = no_helmet
```

复制工程后可以运行完整性检查：

```bash
python tools/check_self_contained.py
```

## 4. PyTorch 训练

执行：

```bash
python tools/train_yolo26.py
```

训练过程：

```text
models/yolo26n.pt
    +
datasets/helmet_dataset
    ↓ 迁移学习
models/best.pt
```

默认训练参数：

```text
输入尺寸：640x640
训练轮数：300
batch：自动
early stopping：50
任务：目标检测
类别数：2
```

`best.pt` 是训练结果最好的 PyTorch 权重，用于 PC 精度验证、继续训练和
ONNX 导出，不能直接在 RV1126 NPU 上执行。

## 5. ONNX 导出

执行：

```bash
python tools/export_onnx.py
```

转换过程：

```text
models/best.pt
    ↓ PyTorch ONNX Export
models/helmet.onnx
```

ONNX 输入为 RGB `640x640`，batch 为 1。

工程内置的 YOLO26 修改过 `Detect.forward()`。ONNX 不直接输出普通的单个
`[1, N, 6]`，而是按三个检测尺度输出 box/class 特征图：

```text
box_p3, class_p3
box_p4, class_p4
box_p5, class_p5
```

共计 6 个输出：

```text
P3：步长 8，主要检测小目标
P4：步长 16，主要检测中等目标
P5：步长 32，主要检测大目标
```

这种导出方式避开了部分端到端 TopK/NMS 算子，更适合老 RKNN-Toolkit，
但需要在 C++ 中完成网格解码和 NMS。

## 6. INT8 量化校准

执行：

```bash
python tools/make_quant_list.py
```

脚本从训练集选择 200 张图片，生成：

```text
datasets/helmet_dataset/rknn_quant.txt
```

这些图片用于统计各层数值范围，将浮点 ONNX 模型转换为 INT8/UINT8 模型。
量化可以降低模型体积和内存带宽，提高 RV1126 NPU 推理速度。

## 7. RKNN 转换

在安装了老版 RKNN-Toolkit 1.x 的环境中执行：

```bash
python tools/convert_rknn.py
```

转换过程：

```text
models/helmet.onnx
    +
datasets/helmet_dataset/rknn_quant.txt
    ↓ RKNN-Toolkit 1.x
models/helmet.rknn
```

主要转换参数：

```text
target_platform = rv1126
mean_values     = [0, 0, 0]
std_values      = [255, 255, 255]
输入格式         = RGB
输入类型         = UINT8
INT8 量化        = 开启
```

板端送入 `0~255` 的 RGB888 数据，RKNN 根据模型配置完成除以 255 的归一化。

`helmet.rknn` 是老 RV1126 RKNPU 使用的二进制模型，不能作为普通 ONNX 模型
在 PC 上直接运行。当前工程尚未生成该文件，因为当前机器没有老 RKNN-Toolkit。

## 8. 板端后处理

模型输出由 `src/ai/helmet_postprocess.cpp` 解析：

```text
6 个 RKNN 输出
    ↓
P3/P4/P5 网格和步长解码
    ↓
box 距离/DFL 解码
    ↓
class sigmoid
    ↓
置信度过滤
    ↓
同类别 NMS
    ↓
letterbox 坐标还原
    ↓
helmet_result_t
```

每个检测结果包含：

```text
class_id
confidence
x, y, w, h
```

后处理同时兼容标准端到端 `[1, N, 6]` 的
`x0, y0, x1, y1, score, class_id` 输出。

## 9. C++ 板端程序编译

模型转换和 C++ 编译是两条独立流程。完整板端程序使用 ARM 交叉编译器构建：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1
```

如果交叉编译器不在 `PATH` 中：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1 \
  G++=/path/to/arm-linux-gnueabihf-g++
```

编译过程：

```text
C++ 源码
  + 老版 rknn_api.h
  + RKMedia/FFmpeg 头文件
  + RV1126 ARM32 动态库
       ↓ arm-linux-gnueabihf-g++
rv1126_detect_helmet_app
```

主要链接库：

```text
librknn_api / librknn_runtime
libeasymedia
librga
librockchip_mpp
libdrm
libavformat / libavcodec / libavutil
libsrt
libssl / libcrypto
libx264
libasound
libv4l2 / libv4lconvert
```

工程使用的头文件和预编译库全部位于：

```text
third_party/rv1126/
```

## 10. 最终部署

板端至少需要：

```text
rv1126_detect_helmet_app
models/helmet.rknn
models/labels.txt
```

运行关系：

```text
rv1126_detect_helmet_app
        │
        ├── 动态加载 models/helmet.rknn
        ├── 打开摄像头
        ├── VENC 编码并推流
        └── RGA 生成 640x640 RGB
                    ↓
                 RKNN NPU
                    ↓
              helmet/no_helmet
```

如果板端缺少匹配的动态库，还需从 `third_party/rv1126/lib` 部署对应 `.so`。
RKNN Runtime、RGA、MPP 和 ISP/AIQ 库应优先与板端驱动版本保持一致。

最终需要明确区分：

```text
rv1126_detect_helmet_app = 板端媒体与检测业务程序
models/helmet.rknn       = NPU 推理模型
```

二者独立构建、独立部署，在程序运行时组合。
