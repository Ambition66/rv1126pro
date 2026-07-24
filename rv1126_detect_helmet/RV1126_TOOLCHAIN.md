# RV1126 工具链说明

本工程已经按 **RV1126 老 RKNPU** 收敛，不按 RK3588/RV1126B 的 RKNPU2 路线设计。

## 1. 平台边界

需要明确区分：

```text
RV1126      -> 老 RKNPU / RKNN-Toolkit 1.x / 老 rknn_api.h
RV1126B     -> RKNPU2 / RKNN-Toolkit2
RK3588      -> RKNPU2 / RKNN-Toolkit2
```

`rknn-toolkit2-master` 主要服务 RK3588、RK356x、RV1103、RV1106、RV1126B 等新平台。它可以参考，但不要作为当前 RV1126 头盔检测工程的默认转换工具。

## 2. 本工程使用的本地 RV1126 资源

当前工程已经发现并复用了这些老 RV1126 资源：

```text
third_party/rv1126/include/rknn/rknn_api.h
third_party/rv1126/lib/platform/librknn_api.so
third_party/rv1126/lib/platform/librknn_runtime.so
tools/convert_rknn.py
```

`Makefile` 默认配置：

```makefile
RKNN_INC ?= ./third_party/rv1126/include/rknn
RKNN_LIB ?= ./third_party/rv1126/lib/platform/librknn_api.so
```

如果实际 RV1126 SDK 的头文件或库路径不同，编译时覆盖这两个变量即可。

## 3. 从 0 到 1 的实际闭环

在数据集准备完成后，当前工程目标闭环是：

```text
导入数据集
  -> 训练生成 models/best.pt
  -> 导出 models/helmet.onnx
  -> 生成量化校准列表
  -> RKNN-Toolkit 1.x 转换 models/helmet.rknn
  -> 交叉编译 rv1126_detect_helmet_app
  -> RV1126 板端加载 RKNN
  -> 摄像头单路推流 + AI 抽帧检测
```

目前欠缺的是本地真实数据集和真实板端联调。代码骨架、脚本、文档和 RV1126 工具链方向已经具备从 0 到 1 的基础。

## 4. 风险点

- 老 RKNN-Toolkit 对新 YOLO 结构支持不一定稳定。
- YOLO26 直接导出的 RKNN 更偏 toolkit2 新平台路线，不建议作为 RV1126 第一版主线。
- 第一版更推荐 YOLOv5n/YOLOv5s，原因是 RV1126 资料、转换脚本、后处理代码都更成熟。
- `HelmetDetector::Postprocess()` 仍需根据真实 RKNN 输出 shape 做最终适配。
- 最终一定要在 RV1126 板端验证 `rknn_init`、输入输出 tensor、推理耗时和长期运行稳定性。

## 5. 推荐验收顺序

1. 先用 `models/helmet.rknn` 做单图 RKNN 推理验证。
2. 确认输出 tensor shape，并完成 YOLO 后处理。
3. 再接入 `ENABLE_RKMEDIA=1` 的实时摄像头链路。
4. 验证 720p 推流稳定，AI 5 FPS 不积压。
5. 连续运行 24 小时观察内存、NPU、RGA、VENC 状态。
