# yolov8 工程介绍

`yolov8` 是一个基于 Rockchip RKNN Runtime 的 YOLOv8 C++ 推理示例工程。它主要演示如何在 Rockchip NPU 平台上加载 `.rknn` 模型，完成图像/视频输入、前处理、NPU 推理、YOLO 后处理、画框输出和多线程推理。

从当前工程配置看，它更偏向 **RK3588 / RKNPU2** 示例，而不是老 RV1126 工程。它对 `rv1126_detect_helmet` 很有参考价值，但不能直接整体搬过去。

## 1. 工程定位

当前工程提供三种典型使用方式：

```text
单张图片推理
视频文件逐帧推理
多线程池推理
```

核心链路如下：

```text
OpenCV 读取图片/视频帧
  -> letterbox
  -> BGR 转 RGB
  -> OpenCV/RGA resize
  -> RKNN Runtime 推理
  -> YOLOv8 后处理
  -> OpenCV 画框
  -> 输出图片或处理后视频帧
```

它是一个完整的 AI 推理 demo，不包含摄像头 VI、VENC 编码、FFmpeg 推流这些媒体工程能力。

## 2. 目录结构

```text
yolov8/
  CMakeLists.txt
  build-rk3588.sh
  result.jpg

  weights/
    yolov8s.float.rknn
    yolov8s.int.rknn
    person_n_relu.int.rknn
    person_s_relu.int.rknn

  images/
    bus.jpg
    person.jpg
    street.jpg

  librknn_api/
    include/
      rknn_api.h
      rknn_matmul_api.h
    aarch64/librknnrt.so
    armhf/librknnrt.so

  3rdparty/
    opencv/
    rga/

  src/
    yolov8_img.cpp
    yolov8_video.cpp
    yolov8_thread_pool.cpp

    engine/
      engine.h
      rknn_engine.h
      rknn_engine.cpp

    process/
      preprocess.h
      preprocess.cpp
      postprocess.h
      postprocess.cpp

    task/
      yolov8_custom.h
      yolov8_custom.cpp
      yolov8_thread_pool.h
      yolov8_thread_pool.cpp

    draw/
      cv_draw.h
      cv_draw.cpp

    types/
    utils/
```

## 3. 核心模块

### 3.1 CMake 构建

`CMakeLists.txt` 默认配置：

```text
LIB_ARCH = aarch64
LIB_ARCH_RGA = gcc-aarch64
DEVICE_NAME = RK3588
```

它链接的是：

```text
librknn_api/aarch64/librknnrt.so
3rdparty/rga/libs/Linux/gcc-aarch64/librga.so
OpenCV
```

这说明当前工程默认更适合 RK3588/aarch64 环境。老 RV1126 通常是 armhf，且 RKNN API/runtime 版本不同，因此不能直接把这套 CMake 当作 RV1126 的构建脚本。

### 3.2 RKNN 引擎封装

核心文件：

```text
src/engine/rknn_engine.cpp
src/engine/rknn_engine.h
src/engine/engine.h
```

`RKEngine` 封装了 RKNN Runtime 的基本流程：

```text
读取 .rknn 文件
  -> rknn_init
  -> rknn_query SDK/runtime 版本
  -> 查询输入 tensor
  -> 查询输出 tensor
  -> rknn_inputs_set
  -> rknn_run
  -> rknn_outputs_get
  -> 拷贝输出到自定义 tensor
  -> rknn_destroy
```

这个封装思路非常值得借鉴。`rv1126_detect_helmet` 里的 `RknnEngine` 就可以吸收它的结构：模型加载、tensor 属性打印、输入输出抽象、错误码和日志分层。

但要注意，`yolov8` 使用的是 RKNPU2 风格的 `librknnrt.so` 和头文件。老 RV1126 要使用对应老版本 `rknn_api.h`、`librknn_api.so`、`librknn_runtime.so`。

### 3.3 前处理

核心文件：

```text
src/process/preprocess.cpp
src/process/preprocess.h
```

前处理支持两条路线：

```text
OpenCV letterbox + resize + BGR2RGB
RGA letterbox/resize + BGR2RGB
```

关键函数：

```text
letterbox()
cvimg2tensor()
letterbox_rga()
cvimg2tensor_rga()
```

对 `rv1126_detect_helmet` 的价值：

- 可以参考 `letterbox` 的比例保持和 padding 逻辑。
- 可以参考 RGA resize 的调用方式。
- 可以参考输入 tensor 与 `cv::Mat` / RGB888 buffer 的转换思路。

但实际 RV1126 实时摄像头链路里，输入更可能来自 RKMedia/RGA 的 NV12/RGB888 buffer，不一定经过 OpenCV。

### 3.4 YOLOv8 后处理

核心文件：

```text
src/process/postprocess.cpp
src/process/postprocess.h
```

后处理支持：

```text
float 输出后处理
int8 量化输出后处理
sigmoid
grid/stride 解码
NMS
坐标归一化
```

当前配置写死了一些 YOLOv8 参数：

```text
input_w = 640
input_h = 640
objectThreshold = 0.2
nmsThreshold = 0.25
headNum = 3
class_num = 5
strides = 8, 16, 32
mapSize = 80x80, 40x40, 20x20
```

这部分是最值得借鉴的模块之一，因为 `rv1126_detect_helmet` 当前的 `HelmetDetector::Postprocess()` 还是通用占位解析。后续拿到真实 `helmet.rknn` 输出 shape 后，可以参考这里补完整 YOLO 解码和 NMS。

需要注意：

- 当前类别是 5 类行人相关类别，不是头盔类别。
- 参数写死在代码里，迁移时要改成跟模型一致。
- YOLOv8 后处理不等于 YOLOv5/YOLO26 后处理，不能机械复制。

### 3.5 业务模型封装

核心文件：

```text
src/task/yolov8_custom.cpp
src/task/yolov8_custom.h
```

`Yolov8Custom` 把完整推理过程封装成：

```text
LoadModel()
  -> 查询输入输出 shape
  -> 分配输入输出 buffer

Run()
  -> Preprocess()
  -> Inference()
  -> Postprocess()
  -> letterbox_decode()
```

这个类的组织方式很清楚，适合映射到 `rv1126_detect_helmet` 的：

```text
HelmetDetector::LoadModel()
HelmetDetector::Run()
HelmetDetector::Preprocess()
HelmetDetector::Inference()
HelmetDetector::Postprocess()
```

### 3.6 画框模块

核心文件：

```text
src/draw/cv_draw.cpp
src/draw/cv_draw.h
```

它使用 OpenCV 在图片上绘制检测框和类别文本。

这适合离线图片/视频 demo，不适合直接用于 RV1126 实时推流画框。RV1126 第一版建议先输出检测结果；如果后续要画框推流，更推荐在编码前用 RGA/RGN/OSD 叠加，而不是依赖 OpenCV 修改整帧像素。

### 3.7 线程池推理

核心文件：

```text
src/task/yolov8_thread_pool.cpp
src/task/yolov8_thread_pool.h
src/yolov8_thread_pool.cpp
```

线程池版本流程：

```text
读取视频帧线程
  -> submitTask()
  -> 多个 YOLOv8 实例并行推理
  -> getTargetImgResult()
  -> 输出处理后帧
```

这个对 RK3588 这类 NPU/CPU 资源更强的平台更有意义。对老 RV1126，第一版不建议多模型实例并行跑，容易加重内存和调度压力。

`rv1126_detect_helmet` 更适合使用：

```text
媒体线程
  -> AiFrameQueue 只保留最新帧
  -> 单 AI worker 推理
```

也就是宁愿降低 AI FPS，也不要让推理任务积压。

## 4. 可运行程序

### 4.1 图片推理

入口：

```text
src/yolov8_img.cpp
```

调用方式大致为：

```bash
./yolov8_img weights/yolov8s.int.rknn images/bus.jpg
```

输出：

```text
result.jpg
```

### 4.2 视频推理

入口：

```text
src/yolov8_video.cpp
```

用于读取视频文件并逐帧推理、画框。

### 4.3 线程池推理

入口：

```text
src/yolov8_thread_pool.cpp
```

调用参数包括：

```text
模型路径
视频路径
线程数
```

它会统计处理 FPS，用于评估多实例推理吞吐。

## 5. 对 rv1126_detect_helmet 的借鉴价值

建议借鉴：

- `RKEngine` 的 RKNN runtime 封装思路。
- 输入/输出 tensor 属性查询和打印方式。
- `Yolov8Custom` 的 `LoadModel/Run/Preprocess/Inference/Postprocess` 类结构。
- `letterbox`、BGR/RGB 转换、RGA resize 的处理思路。
- int8 输出反量化、grid/stride 解码、NMS 后处理代码。
- 图片 demo 作为单图 RKNN 验证入口的组织方式。

不建议直接照搬：

- RK3588/aarch64 CMake 配置。
- `librknnrt.so` 新 runtime 链接方式。
- 多线程池模型实例并行方案。
- OpenCV 视频读取作为板端摄像头链路。
- OpenCV 画框作为实时推流画框方案。
- 写死的 5 类行人类别和 YOLOv8 输出参数。

## 6. 和 rv1126_detect_helmet 的关系

`yolov8` 更像是 AI 推理参考工程：

```text
模型加载
前处理
后处理
单图/视频 demo
线程池
```

`rv1126_detect_helmet` 是实际产品基础工程：

```text
摄像头采集
H.264 编码
FFmpeg 推流
RGA AI 抽帧
RKNN 推理
头盔检测业务判断
后续告警/抓拍/上报
```

因此最佳做法不是把 `yolov8` 整体搬进 `rv1126_detect_helmet`，而是把它拆成几个可吸收的能力：

```text
RKNN 引擎封装
模型输入输出 shape 管理
YOLO 后处理
单图验证工具
RGA 前处理经验
```

然后继续沿用 `rv1126_ffmpeg` 的媒体链路，保持 RV1126 工程的单路推流 + AI 抽帧结构。

## 7. 平台风险

当前 `yolov8` 默认更像 RK3588 示例。迁移到老 RV1126 时需要确认：

- RKNN 模型是否由老 RKNN-Toolkit 1.x 生成。
- `rknn_api.h` 是否与板端 runtime 匹配。
- YOLOv8 的算子和输出结构是否被老 RV1126 工具链支持。
- int8 量化后精度是否可接受。
- 推理速度是否满足 5 FPS 左右的业务需求。

对当前头盔检测项目，第一版仍建议优先 YOLOv5n/YOLOv5s 路线。`yolov8` 的价值主要是工程结构和后处理参考，而不是作为 RV1126 第一版唯一模型路线。
