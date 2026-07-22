# RV1126 电动车驾驶员头盔检测实现方案

## 1. 项目目标

本项目以 `rv1126_ffmpeg` 为媒体基础工程，在 RV1126 板端实现电动车驾驶员头盔佩戴检测能力。

第一阶段目标不是做复杂平台，而是先形成稳定闭环：

- 单路摄像头采集。
- 单路 H.264 视频编码与推流/录像。
- AI 旁路抽帧检测电动车驾驶员是否佩戴头盔。
- 输出检测结果，包括类别、置信度、检测框、时间戳。
- 后续可扩展画框推流、抓拍留证、告警上报。

## 2. 总体判断

当前场景不建议继续保留 `rv1126_ffmpeg` 原有双码流设计。

原因：

- 头盔检测的核心资源消耗在 RGA 前处理、NPU 推理和后处理。
- 双码流会额外占用 VENC、RGA、内存带宽和线程调度资源。
- 对“检测电动车驾驶员是否戴头盔”这一业务，单码流已经足够。
- 第一版应优先保证稳定性、低延迟和可维护性。

推荐架构：

```text
Camera VI
  |
  +--> VENC(H.264) --> FFmpeg/RTMP or local recording
  |
  +--> AI frame branch --> RGA resize/format convert --> RKNN inference --> postprocess --> result output
```

如果后续需要画框推流，再将检测结果回写到编码前的视频帧，或使用 RKMedia OSD/RGN 叠加。

## 3. 推荐模型路线

第一版推荐使用轻量检测模型：

```text
YOLOv5n-helmet or YOLOv5s-helmet
```

不建议第一版直接使用 YOLO26。

原因：

- YOLOv5 在 RKNN/RV1126 上资料和示例更多。
- C/C++ 后处理成熟，方便排查问题。
- INT8 量化更容易跑稳。
- 对头盔检测这种小类别任务足够实用。

推荐类别设计：

```text
rider
helmet
no_helmet
```

如果数据集质量允许，可扩展为：

```text
rider
electric_bike
helmet
no_helmet
```

不建议只做：

```text
helmet
no_helmet
```

因为这样很难判断头盔是否属于电动车驾驶员，容易被行人、安全帽、广告牌、背景物体干扰。

推荐模型输入：

- 第一版：`640x640`
- 性能不足时：降到 `416x416`
- 远距离目标识别不足时：保留 `640x640`，优化模型和前处理

推荐推理帧率：

```text
5-10 FPS
```

视频编码仍可保持 25 FPS，AI 不需要每帧都推理。

## 4. 工程目录建议

在 `rv1126_detect_helmet` 中组织为独立工程，逐步吸收 `rv1126_ffmpeg` 和 RKNN 示例代码：

```text
rv1126_detect_helmet/
  IMPLEMENTATION_PLAN.md
  README.md
  Makefile
  models/
    helmet.rknn
    helmet.onnx
    best.pt
    labels.txt
  datasets/
    helmet_dataset/
      images/
        train/
        val/
      labels/
        train/
        val/
      helmet.yaml
  tools/
    import_dataset.py
    train_yolov5.py
    train_yolo26.py
    export_onnx.py
    convert_onnx_to_rknn.py
    eval_rknn_image.py
    eval_rknn_video.py
  include/
    helmet_detector.h
    helmet_types.h
    ai_frame_queue.h
    ai_result_manager.h
  src/
    main.cpp
    media/
      rv1126_media_init.cpp
      rv1126_media_init.h
      ffmpeg_push.cpp
      ffmpeg_push.h
    ai/
      helmet_detector.cpp
      helmet_postprocess.cpp
      helmet_preprocess.cpp
      ai_frame_queue.cpp
      ai_result_manager.cpp
    common/
      logging.h
      time_utils.h
```

第一阶段也可以先不拆这么细，但建议至少保持三层：

- `media`：摄像头、RGA、VENC、推流。
- `ai`：RKNN、预处理、后处理、检测结果。
- `common`：日志、时间、配置。

## 5. 核心数据流

### 5.1 视频链路

```text
VI 1280x720 or 1920x1080 NV12
  -> VENC H.264
  -> FFmpeg FLV/RTMP or local file
```

第一版建议使用：

```text
1280x720, H.264, 25 FPS
```

如果需要更清晰留证，再升级到：

```text
1920x1080, H.264, 25 FPS
```

### 5.2 AI 链路

```text
VI/RGA frame
  -> AI frame queue, keep latest frame only
  -> RGA resize to model input
  -> color convert NV12/BGR/RGB as required
  -> RKNN inference
  -> YOLO postprocess
  -> helmet result
```

AI 队列策略：

- 队列长度建议为 `1-3`。
- 新帧到来时，如果队列满，丢弃旧帧。
- AI 线程只处理最新帧，避免延迟持续累积。

## 6. 线程模型

建议线程如下：

```text
main thread
  初始化配置、RKMedia、RKNN、FFmpeg

venc_thread
  从 VENC 获取 H.264 数据，写入推流队列

push_thread
  从推流队列取 H.264 packet，写入 FFmpeg

ai_capture_thread
  从 VI/RGA 获取原始帧，提交给 AI 队列

ai_infer_thread
  从 AI 队列取最新帧，运行 RKNN，更新检测结果

optional_result_thread
  打印、保存、上报检测结果
```

如果第一版想更简单，可以把 `ai_capture_thread` 和 `ai_infer_thread` 合并为一个线程，但要确保不会阻塞视频编码链路。

## 7. 可借鉴代码

### 7.1 来自 rv1126_ffmpeg

可复用：

- RKMedia 初始化思路。
- VI、RGA、VENC 创建方式。
- FFmpeg 输出上下文创建方式。
- H.264 packet 推流逻辑。
- pthread 队列模型。

需要调整：

- 删除双码流逻辑。
- 保留单路 VENC。
- 增加 AI 旁路取帧。
- 视频队列增加容量上限。
- 修复资源释放和异常退出路径。

### 7.2 来自 yolov8

可借鉴：

- `rknn_engine` 的封装方式。
- `LoadModel / Preprocess / Inference / Postprocess / Run` 的模型类结构。
- tensor layout、type、zp、scale 的抽象。
- 前处理中的 letterbox、resize、RGA 加速思路。
- 线程池里的任务队列限流思想。

不建议直接照搬：

- RK3588/aarch64 CMake 配置。
- 多模型实例线程池。
- 写死的 YOLOv8 后处理。
- OpenCV 画框作为推流画框方案。

### 7.3 来自 yolo26_helmet

可借鉴：

- 头盔检测数据集配置。
- 类别设计。
- 训练脚本。
- ONNX 导出流程。

不建议第一版直接使用：

- YOLO26 板端部署。
- Python runtime。
- 默认 RK3588 RKNN 导出参数。

## 8. 模块接口草案

### 8.1 检测结果

```cpp
typedef struct {
    int class_id;
    float confidence;
    int x;
    int y;
    int w;
    int h;
} helmet_detection_t;

typedef struct {
    int frame_id;
    int detection_count;
    helmet_detection_t detections[32];
    uint64_t timestamp_ms;
} helmet_result_t;
```

### 8.2 Detector 接口

```cpp
class HelmetDetector {
public:
    int LoadModel(const char* model_path);
    int Run(const unsigned char* frame,
            int width,
            int height,
            int format,
            helmet_result_t* result);
    void Release();
};
```

第一版可以先支持一种输入格式，例如：

```text
RGB888 640x640
```

后续再支持：

```text
NV12 1280x720
```

## 9. 后处理策略

如果使用 YOLOv5：

- 解析 3 个输出 head。
- 根据 anchor、stride 解码框。
- 使用 `conf_threshold` 过滤低置信度结果。
- 使用 NMS 去重。
- 将模型输入坐标映射回原图坐标。
- 根据类别判断是否存在 `no_helmet`。

建议阈值初始值：

```text
confidence threshold: 0.35
nms threshold: 0.45
```

实际部署时需要根据现场数据调整。

业务判断建议：

```text
如果检测到 rider，且 rider 区域附近存在 no_helmet，则判定未佩戴头盔。
如果只检测到 helmet/no_helmet，没有 rider，则结果只作为候选，不直接告警。
连续 N 帧均未佩戴头盔，再触发告警。
```

推荐告警策略：

```text
连续 3 次检测到 no_helmet，且时间窗口小于 2 秒，触发一次告警。
同一目标 10 秒内不重复告警。
```

## 10. 实施阶段

### 阶段 1：模型闭环

目标：

- 在 PC 侧训练或准备 YOLOv5n/YOLOv5s 头盔模型。
- 导出 ONNX。
- 转换为 RV1126 可用 RKNN。
- 在板端跑单图检测 demo。

验收：

- 单图输出正确检测框。
- 类别、置信度、坐标正常。
- RKNN 初始化和释放正常。

### 阶段 2：板端视频 AI 旁路

目标：

- 从 RV1126 摄像头链路取帧。
- RGA/CPU 前处理到模型输入。
- AI 线程持续输出检测结果。
- 视频推流不受 AI 速度影响。

验收：

- 视频推流稳定。
- AI 检测 5-10 FPS。
- AI 慢时不积压。
- 长时间运行无明显内存增长。

### 阶段 3：业务判断

目标：

- 结合 rider、helmet、no_helmet 做业务判断。
- 增加连续帧确认。
- 增加结果日志。
- 可选增加抓拍保存。

验收：

- 降低单帧误报。
- 输出可用于业务系统的数据结构。

### 阶段 4：画框/告警/上报

目标：

- 支持视频画框。
- 支持告警截图。
- 支持 HTTP/MQTT/文件方式上报。

验收：

- 推流画面有检测框。
- 告警图片和检测结果一致。
- 长时间运行稳定。

## 11. 关键风险

### 11.1 模型适配风险

YOLO26 在 RV1126 上的 RKNN 转换和后处理不确定性较高。

规避：

- 第一版优先 YOLOv5n/YOLOv5s。
- 确认 RKNN 输出 tensor 后再写后处理。

### 11.2 性能风险

如果 AI 每帧推理，会拖慢系统。

规避：

- AI 抽帧。
- 队列只保留最新帧。
- 优先 INT8。
- 必要时降低输入尺寸。

### 11.3 前处理 CPU 占用风险

OpenCV resize/color convert 可能占用较高 CPU。

规避：

- 第一版可用 OpenCV 快速验证。
- 稳定后切到 RGA 前处理。

### 11.4 画框链路风险

当前 `rv1126_ffmpeg` 是编码后推流，不能直接在 H.264 packet 上画框。

规避：

- 第一版只输出检测结果。
- 第二版在编码前使用 RGA/OSD/RGN 叠加。

### 11.5 数据集风险

只训练 `helmet/no_helmet` 容易误检。

规避：

- 增加 rider 或 electric_bike 类别。
- 采集真实部署场景数据。
- 单独处理夜间、逆光、雨天、小目标场景。

## 12. 第一版最小可行目标

第一版建议做到：

```text
单路 720p H.264 推流
YOLOv5n helmet INT8 RKNN
AI 5 FPS
输出 rider/helmet/no_helmet 检测结果
连续帧确认未戴头盔
日志打印检测结果
```

暂不做：

- 双码流。
- 多模型。
- 高级跟踪。
- 复杂 Web 服务。
- 强依赖画框推流。

## 13. 推荐下一步

1. 先准备 YOLOv5n/YOLOv5s 头盔 ONNX 和 RKNN 模型。
2. 在 `rv1126_detect_helmet` 中实现单图 RKNN 检测 demo。
3. 从 `rv1126_ffmpeg` 精简出单码流媒体链路。
4. 接入 AI 旁路线程。
5. 跑 24 小时稳定性测试。

## 14. 从 0 到 1 模型训练与转换流程

为了让 `rv1126_detect_helmet` 目录最终具备从数据集到板端部署的完整能力，建议把模型生产流程也纳入本工程。

当前工程已经把头盔图片、标注、数据集 YAML、YOLO26 源码和模型权重全部收拢到目录内。
可以先验证内置数据集：

```bash
python tools/import_dataset.py
```

具体说明见：

```text
DATASET.md
```

完整链路如下：

```text
验证或替换内置头盔数据集
  -> 检查 YOLO 标注格式
  -> 训练 PyTorch 模型 best.pt
  -> 验证 PyTorch 精度
  -> 导出 ONNX
  -> ONNX 简化/检查
  -> 转换 RKNN
  -> PC/板端单图验证
  -> 接入 RV1126 实时视频链路
```

### 14.1 数据集目录规范

建议统一采用 YOLO 标注格式：

```text
rv1126_detect_helmet/
  datasets/
    helmet_dataset/
      images/
        train/
        val/
      labels/
        train/
        val/
      helmet.yaml
```

每张图片对应一个同名 `.txt` 标注文件：

```text
images/train/000001.jpg
labels/train/000001.txt
```

YOLO 标注格式：

```text
class_id x_center y_center width height
```

其中坐标均为 0-1 归一化值。

推荐第一版类别：

```yaml
path: ./datasets/helmet_dataset
train: images/train
val: images/val

nc: 3
names: ["rider", "helmet", "no_helmet"]
```

如果训练数据暂时只有头盔类别，也可以兼容 `yolo26_helmet/helmet.yaml` 的两类设计：

```yaml
path: ./datasets/helmet_dataset
train: images/train
val: images/val

nc: 2
names: ["helmet", "no_helmet"]
```

但两类模型只能判断图中是否存在头盔/未戴头盔，不能可靠判断对象是否为电动车驾驶员。实际产品更推荐三类或四类。

### 14.2 数据采集建议

数据集不要只用网络图片，必须补真实部署场景：

- 白天、夜晚、阴雨、逆光。
- 近距离和远距离。
- 正面、侧面、背面。
- 单人、多人、遮挡。
- 戴普通头盔、半盔、安全帽、帽子、没戴头盔。
- 电动车、摩托车、自行车、行人混杂。

建议最小数据规模：

```text
训练集：3000-8000 张
验证集：500-1500 张
```

如果只是先打通工程，几百张也能跑通流程，但不能代表实际效果。

### 14.3 训练 YOLOv5 推荐流程

第一版产品落地推荐先用 YOLOv5n 或 YOLOv5s。

建议新增脚本：

```text
tools/train_yolov5.py
```

示例：

```python
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

cmd = [
    "python",
    "train.py",
    "--img", "640",
    "--batch", "16",
    "--epochs", "200",
    "--data", str(ROOT / "datasets/helmet_dataset/helmet.yaml"),
    "--weights", "yolov5n.pt",
    "--project", str(ROOT / "runs/train"),
    "--name", "helmet_yolov5n",
]

subprocess.run(cmd, check=True)
```

训练产物通常位于：

```text
runs/train/helmet_yolov5n/weights/best.pt
```

复制到工程模型目录：

```text
models/best.pt
```

### 14.4 训练 YOLO26 参考流程

`yolo26_helmet` 中已有训练参考：

```python
from ultralytics import YOLO

model = YOLO("./yolo26n.pt")

results = model.train(
    data="helmet.yaml",
    epochs=300,
    imgsz=640,
    batch=-1,
    workers=4,
    patience=50,
    device=0,
    task="detect",
    mode="train",
    verbose=True
)
```

迁移到本工程后建议新增：

```text
tools/train_yolo26.py
```

当前目录已提供该脚本，详细训练说明见：

```text
TRAINING.md
```

默认训练产物会复制为：

```text
models/best.pt
```

示例：

```python
from ultralytics import YOLO

model = YOLO("yolo26n.pt")

model.train(
    data="datasets/helmet_dataset/helmet.yaml",
    epochs=300,
    imgsz=640,
    batch=-1,
    workers=4,
    patience=50,
    device=0,
    task="detect",
    mode="train",
    project="runs/train",
    name="helmet_yolo26n",
)
```

训练完成后保存：

```text
runs/train/helmet_yolo26n/weights/best.pt
models/best.pt
```

注意：

- YOLO26 更适合作为第二阶段实验。
- 第一版如果追求 RV1126 稳定落地，仍建议优先 YOLOv5n/YOLOv5s。

### 14.5 导出 ONNX

建议新增统一脚本：

```text
tools/export_onnx.py
```

当前目录已提供该脚本，详细命令见：

```text
ONNX_EXPORT.md
```

默认约定：

```text
输入：models/best.pt
输出：models/helmet.onnx
```

#### YOLOv5 ONNX 导出

在 YOLOv5 工程中执行：

```bash
python export.py --weights models/best.pt --img 640 --batch 1 --include onnx --opset 12 --simplify
```

导出结果：

```text
models/best.onnx
```

复制或重命名为：

```text
models/helmet.onnx
```

#### YOLO26 ONNX 导出

参考 `yolo26_helmet/export.py`：

```python
from ultralytics import YOLO

model = YOLO("models/best.pt")
model.export(format="onnx", imgsz=640, batch=1)
```

如果使用 YOLO26 one-to-one 默认头，输出通常更接近端到端检测结果，后处理更简单；如果使用传统 one-to-many 头，则需要 NMS 后处理：

```python
model.export(format="onnx", imgsz=640, batch=1, end2end=False)
```

选择建议：

- 优先尝试默认 one-to-one 导出，减少板端后处理复杂度。
- 如果 RKNN 转换或输出解析不稳定，再尝试 `end2end=False`。

导出后建议固定输出名：

```text
models/helmet.onnx
```

### 14.6 ONNX 检查

转换 RKNN 前必须检查 ONNX 是否可用：

```bash
python -m onnxsim models/helmet.onnx models/helmet_sim.onnx
```

如果工具链不支持 `onnxsim`，至少执行一次 ONNXRuntime 或 Ultralytics 推理对比：

```bash
yolo predict model=models/helmet.onnx source=images/test.jpg
```

检查项：

- 输入尺寸是否为 `1x3x640x640` 或 `1x640x640x3`。
- 输出数量和 shape 是否符合预期。
- 类别数是否正确。
- ONNX 推理结果和 PyTorch `best.pt` 是否接近。

### 14.7 转换 RKNN

RKNN 转换建议在 x86 Linux 主机完成，不建议在板端转换。

#### YOLO26 直接导出 RKNN

`yolo26_helmet` 文档中支持：

```python
from ultralytics import YOLO

model = YOLO("models/best.pt")
model.export(format="rknn", name="rk3588")
```

文档中可选 `name` 包括：

```text
rk3588, rk3576, rk3566, rk3568, rk3562,
rv1103, rv1106, rv1103b, rv1106b, rk2118, rv1126b
```

注意：

- 该导出流程面向 RKNN Toolkit2。
- 当前工程目标是 RV1126，不是 RV1126B。
- 如果使用老 RV1126 工具链，可能不能直接使用 `name="rv1126b"` 的产物。
- 对本项目，YOLO26 直接导出 RKNN 作为实验路线，不作为第一版唯一依赖。

#### ONNX 转 RKNN 脚本

建议新增：

```text
tools/convert_onnx_to_rknn.py
```

示例模板：

```python
from rknn.api import RKNN

ONNX_MODEL = "models/helmet.onnx"
RKNN_MODEL = "models/helmet.rknn"
DATASET = "datasets/quant_dataset.txt"

rknn = RKNN(verbose=True)

rknn.config(
    mean_values=[[0, 0, 0]],
    std_values=[[255, 255, 255]],
    target_platform="rv1126",
)

ret = rknn.load_onnx(model=ONNX_MODEL)
if ret != 0:
    raise RuntimeError("load_onnx failed")

ret = rknn.build(
    do_quantization=True,
    dataset=DATASET,
)
if ret != 0:
    raise RuntimeError("rknn build failed")

ret = rknn.export_rknn(RKNN_MODEL)
if ret != 0:
    raise RuntimeError("export_rknn failed")

rknn.release()
```

如果本地 RKNN Toolkit 不支持 `target_platform="rv1126"`，需要使用与板端 NPU runtime 匹配的 RKNN Toolkit/Toolkit2 版本，并以实际工具支持列表为准。

### 14.8 量化数据集

INT8 量化需要准备代表性图片列表：

```text
datasets/quant_dataset.txt
```

内容示例：

```text
datasets/helmet_dataset/images/train/000001.jpg
datasets/helmet_dataset/images/train/000002.jpg
datasets/helmet_dataset/images/train/000003.jpg
```

建议数量：

```text
100-500 张
```

要求：

- 覆盖真实部署场景。
- 覆盖白天、夜晚、逆光、远近目标。
- 不要只放清晰正样本。

如果 INT8 精度下降明显，可先测试非量化 RKNN，再做混合量化或重新选择量化样本。

### 14.9 RKNN 模型验证

转换后要先做单图验证，再接视频链路。

建议新增：

```text
tools/eval_rknn_image.py
```

验证内容：

- `helmet.rknn` 能加载。
- 输入尺寸正确。
- 输出 tensor 数量和 shape 正确。
- 检测框能映射回原图。
- 结果与 ONNX/PyTorch 大体一致。

验收命令示例：

```bash
python tools/eval_rknn_image.py --model models/helmet.rknn --image test.jpg
```

板端 C++ 也需要一个最小验证程序：

```text
helmet_rknn_image_test
```

用途：

- 读取本地图片。
- 调用 `HelmetDetector::Run()`。
- 打印检测结果。
- 可选保存画框结果。

只有单图 C++ RKNN 测通后，再接 RV1126 摄像头实时链路。

### 14.10 板端部署文件

部署到 RV1126 板端时至少需要：

```text
rv1126_detect_helmet_app
models/helmet.rknn
models/labels.txt
required .so files
```

`labels.txt` 示例：

```text
rider
helmet
no_helmet
```

启动命令建议：

```bash
./rv1126_detect_helmet_app \
  --model ./models/helmet.rknn \
  --labels ./models/labels.txt \
  --stream rtmp://192.168.1.66:1935/live/helmet \
  --ai-fps 5 \
  --width 1280 \
  --height 720
```

## 15. 最终目录交付清单

为了让本目录从 0 到 1 可复现，最终建议至少包含：

```text
rv1126_detect_helmet/
  README.md
  IMPLEMENTATION_PLAN.md
  Makefile
  models/
    labels.txt
    best.pt
    helmet.onnx
    helmet.rknn
  datasets/
    helmet_dataset/
      helmet.yaml
      images/
      labels/
    quant_dataset.txt
  tools/
    import_dataset.py
    train_yolov5.py
    train_yolo26.py
    export_onnx.py
    convert_onnx_to_rknn.py
    eval_rknn_image.py
  include/
  src/
```

大文件如数据集、`.pt`、`.onnx`、`.rknn` 可以不提交 Git，但目录结构、脚本和说明必须保留。

推荐 `.gitignore`：

```text
datasets/helmet_dataset/images/
datasets/helmet_dataset/labels/
models/*.pt
models/*.onnx
models/*.rknn
runs/
build/
```

## 16. 从 0 到 1 验收标准

完整验收分为 5 个层级：

1. 数据集验收

```text
helmet.yaml 可被训练脚本读取
训练集/验证集路径正确
类别数和 labels.txt 一致
```

2. 训练验收

```text
成功生成 best.pt
验证集 mAP 和误检情况可接受
```

3. ONNX 验收

```text
成功生成 helmet.onnx
ONNX 推理结果与 best.pt 接近
```

4. RKNN 验收

```text
成功生成 helmet.rknn
单图 RKNN 推理正常
输出 shape 和后处理匹配
```

5. 板端验收

```text
单路视频推流稳定
AI 5-10 FPS
检测结果稳定输出
连续运行 24 小时无明显内存增长
```
