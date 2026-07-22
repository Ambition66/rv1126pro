# rv1126_detect_helmet

## 当前平台结论

本工程当前按 **老 RV1126** 落地：模型转换默认使用 RKNN-Toolkit 1.x，板端运行默认使用 RV1126 SDK 内的老 `rknn_api.h`、`librknn_api.so`、`librknn_runtime.so`。

`rknn-toolkit2-master` 主要面向 RK3588、RV1126B 等 RKNPU2 平台，在本工程中只作为概念和流程参考，不作为 RV1126 第一版主工具链。RV1126 相关工具链细节见 `RV1126_TOOLCHAIN.md`，RKNN 转换步骤见 `RKNN_CONVERT.md`。

`rv1126_detect_helmet` 是一个面向 **RV1126 板端电动车驾驶员头盔佩戴检测** 的工程。

本目录已经自包含训练数据、YOLO26 源码与权重、ONNX 模型、RV1126 头文件和
ARM32 预编译库，不再依赖工作区里的其他兄弟工程。完整复现步骤见
[`SELF_CONTAINED.md`](SELF_CONTAINED.md)。

它不是单纯的模型 demo，而是按实际产品链路设计的基础工程：摄像头采集一路视频用于推流，同时旁路抽帧给 AI 检测驾驶员是否佩戴头盔。

## 1. 工程目标

当前目标是做：

```text
单路视频推流 + AI 抽帧头盔检测
```

不做双码流。原因是头盔检测业务只需要一路可预览/存证的视频流，AI 分支只需要图像帧，不需要再编码成第二路视频。省掉低码流可以减少 VENC、RGA、内存带宽和线程调度开销，更适合 RV1126 这种资源有限的平台。

最终主链路设计如下：

```text
Camera VI
  |
  +--> VENC H264
  |      |
  |      +--> FFmpeg 推流
  |
  +--> RGA RGB888 640x640
         |
         +--> AiFrameQueue
                |
                +--> AiWorker
                       |
                       +--> HelmetDetector
                              |
                              +--> RknnEngine
                                     |
                                     +--> helmet.rknn
```

## 2. 参考工程

本工程主要参考了当前工作目录下的两个已有工程。

`rv1126_ffmpeg` 提供媒体链路参考：

```text
VI 摄像头采集
RGA 图像处理
VENC H264 编码
FFmpeg 推流
RTMP / MPEG-TS 输出
```

原工程是高低双码流结构：

```text
VI -> VENC0 -> 高码流推流
VI -> RGA -> VENC1 -> 低码流推流
```

本工程裁剪为：

```text
VI -> VENC0 -> 唯一视频流
VI -> RGA -> AI 检测帧
```

`yolov8` 提供 AI 推理参考：

```text
RKNN runtime 封装
模型加载
输入输出 tensor 查询
前处理结构
后处理结构
模型类组织方式
```

本工程参考它的思路，重新整理了 `RknnEngine` 和 `HelmetDetector`。

## 3. 当前目录结构

```text
rv1126_detect_helmet/
  README.md
  IMPLEMENTATION_PLAN.md
  DATASET.md
  TRAINING.md
  ONNX_EXPORT.md
  RKNN_CONVERT.md
  Makefile

  datasets/
    helmet_dataset/

  third_party/
    yolo26/
    rv1126/

  models/

  include/
    helmet_types.h
    helmet_detector.h
    nn_types.h
    nn_engine.h
    rknn_engine.h
    ai_frame_queue.h
    ai_result_manager.h
    ai_worker.h
    media_pipeline.h

  src/
    main.cpp
    ai/
      ai_frame_queue.cpp
      ai_result_manager.cpp
      ai_worker.cpp
      helmet_detector.cpp
      rknn_engine.cpp
    media/
      media_pipeline.cpp

  tools/
    import_dataset.py
    train_yolo26.py
    export_onnx.py
    make_quant_list.py
    convert_rknn.py
```

## 4. 文档说明

- `IMPLEMENTATION_PLAN.md`：整体实现方案，包含平台选择、模型选择、单路码流设计和工程落地步骤。
- `DATASET.md`：数据集导入说明。
- `TRAINING.md`：训练生成 `models/best.pt` 的说明。
- `ONNX_EXPORT.md`：从 `best.pt` 导出 `models/helmet.onnx` 的说明。
- `RKNN_CONVERT.md`：从 ONNX 转换 `models/helmet.rknn` 的说明。
- `MODEL_PIPELINE.md`：从初始权重、训练、ONNX/RKNN 转换到 C++ 编译部署的完整演变。
- `SELF_CONTAINED.md`：复制本目录后独立复现所需的环境和操作步骤。

## 5. 模型产物流程

完整模型链路如下：

```text
数据集
  -> 训练 best.pt
  -> 导出 helmet.onnx
  -> 生成 RKNN 量化校准列表
  -> 转换 helmet.rknn
```

对应命令：

```bash
python tools/import_dataset.py
python tools/train_yolo26.py
python tools/export_onnx.py
python tools/make_quant_list.py
python tools/convert_rknn.py
```

各脚本作用：

- `tools/import_dataset.py`：验证内置数据集，或按需导入外部数据集。
- `tools/train_yolo26.py`：使用 `third_party/yolo26` 中的源码训练，输出 `models/best.pt`。
- `tools/export_onnx.py`：把 `best.pt` 导出为 `models/helmet.onnx`。
- `tools/make_quant_list.py`：从训练图片中生成 RKNN int8 量化校准列表。
- `tools/convert_rknn.py`：调用 Rockchip RKNN Toolkit 转换得到 `models/helmet.rknn`。

板端最终使用的模型文件是：

```text
models/helmet.rknn
```

## 6. 核心模块说明

### 6.1 公共数据结构

`include/helmet_types.h` 定义工程通用数据结构：

```text
helmet_frame_t       AI 输入帧
helmet_result_t      单帧检测结果
helmet_detection_t   单个检测框
HelmetImageFormat    图像格式
HelmetClassId        类别 ID
```

当前主要类别是：

```text
helmet
no_helmet
```

预留了：

```text
rider
electric_bike
```

后续如果模型升级为“人/车/头盔”联合检测，可以继续使用。

### 6.2 AI 最新帧队列

`AiFrameQueue` 是 AI 分支的帧缓存。

它不是普通 FIFO 队列，而是“只保留最新一帧”的队列：

```text
新帧到来 -> 替换旧帧
推理线程 -> 永远拿最新帧
```

这样做是为了避免 RV1126 上推理速度低于摄像头帧率时产生严重延迟。

### 6.3 AI 工作线程

`AiWorker` 是 AI 推理线程：

```text
PopLatest()
  -> HelmetDetector::Run()
  -> AiResultManager::Update()
```

它把媒体采集线程和模型推理线程解耦。媒体线程只负责送帧，AI 线程自己消费最新帧。

### 6.4 RKNN 引擎封装

`RknnEngine` 封装底层 RKNN API：

```text
读取 rknn 文件
rknn_init
查询输入 tensor
查询输出 tensor
rknn_inputs_set
rknn_run
rknn_outputs_get
rknn_outputs_release
rknn_destroy
```

它通过 `ENABLE_RKNN` 开关控制是否真实链接 RKNN：

- `ENABLE_RKNN=1`：真实调用 RKNN API。
- `ENABLE_RKNN=0`：不链接 RKNN，只保留业务骨架。

### 6.5 头盔检测器

`HelmetDetector` 是检测业务类，负责把一帧图像变成检测结果：

```text
LoadModel()
  -> 查询模型输入输出
  -> 分配输入输出 buffer

Run()
  -> Preprocess()
  -> Inference()
  -> Postprocess()
```

当前前处理支持：

```text
RGB888
BGR888
```

媒体链路里 RGA 默认输出 `RGB888 640x640`，所以可以直接送入检测器。

当前后处理已按内置 `third_party/yolo26` 的定制导出格式实现。该实现于
`Detect.forward()` 的 ONNX 导出分支中按 P3/P4/P5 输出：

```text
box_p3, class_p3, box_p4, class_p4, box_p5, class_p5
```

板端解析包含网格/步长解码、`reg_max=1` 及通用 DFL 解码、sigmoid、置信度过滤、
按类别 NMS 和 letterbox 坐标还原。同时兼容标准 YOLO26 端到端导出的：

```text
[1, N, 6] = x0, y0, x1, y1, score, class_id
```

### 6.6 媒体链路

`MediaPipeline` 封装板端媒体链路。

它有两种模式：

```text
ENABLE_RKMEDIA=0  只保留 stub，方便无 SDK 环境阅读和业务层编译
ENABLE_RKMEDIA=1  启用真实 RKMedia/FFmpeg 链路
```

`ENABLE_RKMEDIA=1` 时，初始化流程为：

```text
RK_MPI_SYS_Init
RK_MPI_VI_SetChnAttr
RK_MPI_VI_EnableChn
RK_MPI_VENC_CreateChn
RK_MPI_RGA_CreateChn
RK_MPI_SYS_Bind(VI, VENC)
RK_MPI_SYS_Bind(VI, RGA)
```

启动后有三个线程：

```text
VencLoop()      从 VENC 获取 H264 编码包
PushLoop()      从内部队列取 H264 包并用 FFmpeg 推流
AiFrameLoop()   从 RGA 获取 RGB888 图像，按 ai-fps 抽帧送 AI
```

## 7. 构建方式

在 RV1126 SDK 交叉编译环境中构建完整板端程序：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1
```

如果交叉编译器路径不同：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1 G++=/path/to/arm-linux-gnueabihf-g++
```

如果只想编译业务骨架，不链接 RKNN 和 RKMedia：

```bash
make stub
```

## 8. 运行方式

示例：

```bash
./rv1126_detect_helmet_app \
  --model ./models/helmet.rknn \
  --stream rtmp://192.168.1.66:1935/live/helmet \
  --protocol flv \
  --width 1280 \
  --height 720 \
  --fps 25 \
  --ai-fps 5 \
  --ai-width 640 \
  --ai-height 640 \
  --conf 0.35 \
  --nms 0.45
```

参数说明：

- `--model`：RKNN 模型路径。
- `--stream`：推流地址。
- `--protocol`：推流封装类型，`flv` 对应 RTMP/FLV，`ts` 对应 MPEG-TS/SRT 类场景。
- `--width` / `--height`：主视频流分辨率。
- `--fps`：主视频流帧率。
- `--ai-fps`：AI 抽帧检测帧率。
- `--ai-width` / `--ai-height`：RGA 输出给 AI 的图像尺寸，默认建议 `640x640`。
- `--conf`：检测置信度阈值，默认 `0.35`。
- `--nms`：同类别 NMS IoU 阈值，默认 `0.45`。

## 9. 当前已完成内容

当前工程已经完成以下主干：

```text
数据集导入
  -> 模型训练 best.pt
  -> ONNX 导出 helmet.onnx
  -> RKNN 转换脚本与校准列表
  -> RKNN 模型加载
  -> AI 最新帧队列
  -> AI 推理线程
  -> YOLO26 六输出/端到端输出后处理
  -> 检测结果缓存
  -> 单路 VI/VENC/FFmpeg 推流
  -> RGA AI 图像分支
```

工程已经包含训练数据、`best.pt`、`helmet.onnx`、量化列表、转换脚本和板端依赖，
可以独立进入老 RKNN-Toolkit 转换及 RV1126 SDK 编译联调阶段。由于当前机器没有
老 RKNN-Toolkit，`models/helmet.rknn` 尚未生成，复现时需按 `RKNN_CONVERT.md` 执行一次转换。

## 10. 后续板端联调重点

还需要在真实 RV1126 SDK/板端环境中确认：

- `ENABLE_RKMEDIA=1` 下 RKMedia/FFmpeg 是否能完整编译链接。
- 摄像头节点 `rkispp_scale0` 是否匹配实际板子的摄像头配置。
- RGA 输出 `RGB888 640x640` 是否与实际驱动行为一致。
- `helmet.rknn` 的真实输出 tensor 顺序和 layout 是否与 ONNX 一致。
- 用同一张图片对比 PyTorch、ONNX、RKNN 的框坐标和置信度。
- 增加未戴头盔报警、截图、叠框、上报等业务逻辑。

下一步建议先在 RV1126 SDK 环境执行：

```bash
make ENABLE_RKNN=1 ENABLE_RKMEDIA=1
```

然后根据真实编译错误、摄像头节点、RGA 输出和 RKNN 输出 shape 继续适配。
