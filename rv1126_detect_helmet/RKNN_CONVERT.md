# RKNN 转换说明

本工程目标平台是 **老 RV1126**，不是 RV1126B、RK3588 这类 RKNPU2 平台。

因此第一优先转换路线是：

```text
models/helmet.onnx
  -> RKNN-Toolkit 1.x
  -> models/helmet.rknn
  -> RV1126 板端 librknn_runtime / librknn_api
```

`rknn-toolkit2-master` 可以作为 RKNN API、量化概念、模型转换流程的参考，但不作为本工程 RV1126 的主转换工具链。

## 1. 工具链要求

RV1126 使用老一代 RKNPU/RKNN runtime。转换模型时需要注意：

- PC 端使用 Rockchip **RKNN-Toolkit 1.x**。
- 板端使用 RV1126 SDK 中匹配的 `librknn_runtime.so` / `librknn_api.so`。
- RKNN-Toolkit 版本不要高于板端 runtime 太多，避免生成板端无法加载的 `.rknn`。
- 本仓库可参考 `rv1126_AI/README.md` 中的经验：`RKNN-Toolkit 1.7.3` 对应板端 `librknn_runtime 1.7.5`。
- 本工程 `Makefile` 默认使用 `third_party/rv1126` 内置的旧版 `rknn_api.h` 和 ARM32 RKNN 库。

不要把 `rv1126b` 当成 `rv1126` 使用。`rv1126b` 属于更新工具链支持的平台名，和老 RV1126 的部署链路不是一回事。

## 2. 准备量化图片列表

先导入数据集，再生成 RKNN INT8 量化校准图片列表：

```bash
python tools/make_quant_list.py \
  --images datasets/helmet_dataset/images/train \
  --output datasets/helmet_dataset/rknn_quant.txt \
  --limit 200
```

量化图片要来自真实头盔/未戴头盔场景。不要使用空目录，也不要只放几张重复图片。

## 3. ONNX 转 RKNN

在安装了老 RKNN-Toolkit 的转换机上执行：

```bash
python tools/convert_rknn.py \
  --onnx models/helmet.onnx \
  --output models/helmet.rknn \
  --dataset datasets/helmet_dataset/rknn_quant.txt \
  --target rv1126
```

如果是 YOLOv5 三输出头，可能需要显式指定 ONNX 输出名：

```bash
python tools/convert_rknn.py \
  --onnx models/helmet.onnx \
  --output models/helmet.rknn \
  --dataset datasets/helmet_dataset/rknn_quant.txt \
  --target rv1126 \
  --outputs output0,output1,output2
```

快速验证转换链路时，可以先导出非量化 RKNN：

```bash
python tools/convert_rknn.py \
  --onnx models/helmet.onnx \
  --output models/helmet.fp.rknn \
  --target rv1126 \
  --no-quant
```

非量化模型主要用于排查模型结构和输出 shape，不建议作为最终产品部署版本。

## 4. 转换脚本参数

当前 `tools/convert_rknn.py` 默认走 RV1126 老 RKNN-Toolkit 配置：

```text
--toolkit rv1126
--target rv1126
mean_values=[[0, 0, 0]]
std_values=[[255, 255, 255]]
reorder_channel="0 1 2"
optimization_level=3
output_optimize=1
quantize_input_node=True
force_builtin_perm=True
```

这些参数参考了 `rv1126_AI/model_trans/onnx_convert.py`，适合 YOLOv5/RV1126 这条路线。

脚本保留了 `--toolkit toolkit2`，只是为了兼容后续在 RK3588/RV1126B 上做实验。本工程当前不要优先使用它。

## 5. 板端运行

把最终模型放到：

```text
rv1126_detect_helmet/models/helmet.rknn
```

然后在 RV1126 SDK/板端环境编译运行：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1

./rv1126_detect_helmet_app \
  --model ./models/helmet.rknn \
  --stream rtmp://192.168.1.66:1935/live/helmet \
  --width 1280 \
  --height 720 \
  --fps 25 \
  --ai-fps 5
```

## 6. 当前后处理状态

`HelmetDetector::Postprocess()` 已适配内置 `third_party/yolo26` 定制导出的 6 个输出：

```text
box_p3, class_p3, box_p4, class_p4, box_p5, class_p5
```

实现包含 `reg_max=1`/DFL 距离解码、P3/P4/P5 网格和步长换算、sigmoid、
置信度过滤、按类别 NMS 以及 letterbox 坐标还原。同时兼容标准端到端
`[1, N, 6]` 的 `xyxy + score + class_id` 输出。

模型上板后仍需检查启动时打印的 6 个 tensor shape 和顺序，并用同一张图片
对比 PyTorch、ONNX、RKNN 结果。其他 YOLOv5 三头格式不在本后处理器的兼容范围内。
