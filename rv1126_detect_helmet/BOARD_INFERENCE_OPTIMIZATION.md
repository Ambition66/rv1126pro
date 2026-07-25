# RV1126 板端推理优化记录

## 第一阶段：已完成

本阶段优先处理影响检测正确性、实时延迟和长时间稳定性的项目，不引入
RK3588 使用的多模型线程池。

### 1. 保持摄像头宽高比

媒体链路不再把 `1280x720` 直接拉伸成 `640x640`。RGA 先等比例缩放到
模型画布内部，例如：

```text
1280x720 -> RGA 640x360 RGB888 -> CPU 上下填充 140 -> 640x640
```

这样与 YOLO letterbox 训练/推理方式一致，后处理也能按照相同的
`scale/pad` 规则映射检测框。

### 2. RGB 预处理快速路径

`HelmetDetector::Preprocess()` 增加两条快速路径：

- 输入尺寸与模型完全一致时，整帧 `memcpy`。
- RGA 已经完成等比例缩放时，按行复制到 letterbox 有效区域。

只有非标准尺寸或 BGR 输入才执行逐像素最近邻缩放。

### 3. AI 帧复用缓冲池

`AiFrameQueue` 使用 3 个按需扩容、重复利用的缓冲区。正常运行不再为每一
帧执行大块 `malloc/free`，同时保留“只处理最新帧”的低延迟策略。

消费者从队列取帧后必须调用：

```cpp
queue.ReleaseFrame(&frame);
```

### 4. 精确 AI 抽帧

原来的整数除法在 `25 FPS -> 15 FPS` 等配置下会错误地发送 25 FPS。现在
改用累加器调度，可以处理不能整除的帧率组合。

### 5. 分阶段耗时

每成功处理 30 帧输出一次平均耗时：

```text
AI perf avg(30): preprocess=...ms inference=...ms postprocess=...ms total=...ms
```

这组数据用于判断下一步应该优化输入复制、RKNN 推理还是输出后处理。

### 6. YOLO26 `reg_max=1`

已确认 YOLO26 在 `reg_max=1` 时使用直接 `ltrb` 距离，只有
`reg_max>1` 才执行 DFL softmax。现有 C++ 逻辑符合这一规则，并增加了
对应说明和单元测试。

### 7. 量化输出直接后处理

六输出 YOLO26 模型如果输出属性全部为 INT8/UINT8 且具有有效的
`scale/zero_point`，程序会自动设置 `want_float=false`，后处理按需读取并
反量化张量值，不再要求 RKNN Runtime 先把全部输出转换成 float32。

单输出端到端模型、浮点模型或量化参数无效时自动回退 float 路径。当前已用
合成 INT8 张量验证 `reg_max=1`、分类 sigmoid 和检测框解码。

## 本地验证

已完成：

- 非 RKNN/RKMedia stub 完整编译。
- YOLO26 六输出、`reg_max=1`、NMS、letterbox 映射测试。
- AI 最新帧覆盖、缓冲区释放和缓冲区复用测试。

在 Linux 主机可以执行：

```bash
make stub
make test-host
```

当前 Windows 环境没有 RV1126 ARM 交叉编译器，因此启用
`ENABLE_RKNN=1 ENABLE_RKMEDIA=1` 的最终链接仍需在配置好 RV1126
工具链的 Linux 环境验证。

## 第二阶段：需要真实 `helmet.rknn` 和开发板

按以下顺序继续：

1. 在 RV1126 上打印输入输出 tensor 的形状、类型、scale 和 zero point。
2. 记录预处理、`rknn_inputs_set`、`rknn_run`、`rknn_outputs_get`、
   后处理的基线耗时。
3. 验证真实模型是否进入 `output_mode=quantized`。
4. 在量化域预过滤分类置信度，减少 sigmoid 和反量化次数。
5. 让后处理直接读取 RKNN 输出 buffer，消除当前输出 `memcpy`。
6. 对比 `640x640` 与静态 `640x384` 模型的精度和板端耗时。
7. 最后评估老 RV1126 Runtime 是否具备可用的 DMA/零拷贝输入接口。

不建议在 RV1126 上增加多个 RKNN 上下文。当前应继续使用：

```text
单 RKNN 上下文 + 单 AI 工作线程 + 最新帧覆盖
```
