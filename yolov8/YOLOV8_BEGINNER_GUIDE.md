# YOLOv8 RKNN 工程零基础讲解

这份文档只讲 `yolov8` 这个工程的代码结构和工作流程。目标读者是：

- 没接触过模型部署的人
- 没接触过 C++ 工程的人
- 想知道“一张图片是怎么经过模型变成检测框”的人

这个工程不是训练 YOLOv8 的工程，而是一个 **把已经转换好的 `.rknn` 模型跑在 Rockchip NPU 上的 C++ 推理工程**。

## 1. 这个工程在做什么

一句话：

```text
读取图片或视频
  -> 调用 YOLOv8 RKNN 模型
  -> 找出图里的目标
  -> 把检测框画回图片或视频
```

这里的 `.rknn` 是 Rockchip 平台使用的模型格式。普通训练框架里常见的是 `.pt`、`.onnx`，但 Rockchip NPU 不能直接跑这些模型，通常要先转换成 `.rknn`。

本工程已经准备好了模型文件：

```text
yolov8/weights/
  yolov8s.float.rknn
  yolov8s.int.rknn
  person_n_relu.int.rknn
  person_s_relu.int.rknn
```

所以这个工程重点是 **部署和推理**，不是训练。

## 2. 先看目录结构

```text
yolov8/
  CMakeLists.txt
  build-rk3588.sh

  weights/
    模型文件，后缀是 .rknn

  images/
    测试图片

  librknn_api/
    Rockchip RKNN Runtime 的头文件和库

  3rdparty/
    opencv/
    rga/

  src/
    yolov8_img.cpp
    yolov8_video.cpp
    yolov8_thread_pool.cpp

    task/
      yolov8_custom.h
      yolov8_custom.cpp
      yolov8_thread_pool.h
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

    draw/
      cv_draw.h
      cv_draw.cpp

    types/
      datatype.h
      yolo_datatype.h
      error.h

    utils/
      logging.h
      engine_helper.h
```

可以先把它理解成几层：

```text
入口层       src/yolov8_img.cpp、src/yolov8_video.cpp
任务封装层   src/task/yolov8_custom.cpp
推理引擎层   src/engine/rknn_engine.cpp
前处理层     src/process/preprocess.cpp
后处理层     src/process/postprocess.cpp
画图层       src/draw/cv_draw.cpp
数据类型层   src/types/
```

## 3. 对 C++ 工程的最小理解

如果你没接触过 C++，先记住几个概念就够了。

### 3.1 `.cpp` 和 `.h`

```text
.h    通常是声明：告诉别人“我有什么类、函数、结构体”
.cpp  通常是实现：真正写函数里面做什么
```

比如：

```text
src/task/yolov8_custom.h
src/task/yolov8_custom.cpp
```

这两个文件是一对。

`.h` 里面声明 `Yolov8Custom` 这个类有哪些函数：

```cpp
nn_error_e LoadModel(const char *model_path);
nn_error_e Run(const cv::Mat &img, std::vector<Detection> &objects);
```

`.cpp` 里面写这些函数具体怎么做。

### 3.2 `class` 是什么

`class` 可以理解成“把数据和操作这些数据的函数放在一起”。

本工程里最重要的类是：

```cpp
class Yolov8Custom
```

它代表一个 YOLOv8 检测器。你可以让它：

```text
加载模型
处理图片
执行推理
输出检测结果
```

### 3.3 `struct` 是什么

`struct` 可以理解成一组数据的打包。

比如检测结果 `Detection`：

```cpp
struct Detection
{
    int class_id;
    std::string className;
    float confidence;
    cv::Scalar color;
    cv::Rect box;
};
```

意思是一个检测框里面包含：

```text
类别 ID
类别名字
置信度
颜色
矩形框位置
```

## 4. 三个程序入口

工程编译后会生成 3 个可执行程序。

### 4.1 单张图片推理

入口文件：

```text
src/yolov8_img.cpp
```

它做的事很简单：

```text
读取模型路径
读取图片路径
创建 Yolov8Custom 对象
加载模型
运行检测
画框
保存 result.jpg
```

核心代码可以概括成：

```cpp
Yolov8Custom yolo;
yolo.LoadModel(model_file);
yolo.Run(img, objects);
DrawDetections(img, objects);
cv::imwrite("result.jpg", img);
```

如果只想理解整个项目，建议从这个文件开始。

### 4.2 视频推理

入口文件：

```text
src/yolov8_video.cpp
```

它和图片推理很像，只是多了一个循环：

```text
打开视频
while 还能读到视频帧:
  读取一帧图片
  YOLOv8 推理
  画框
  可选保存到新视频
```

视频本质上就是很多张图片连续播放，所以视频推理就是对每一帧做图片推理。

### 4.3 线程池推理

入口文件：

```text
src/yolov8_thread_pool.cpp
src/task/yolov8_thread_pool.cpp
```

线程池版本是为了提高吞吐量。

普通版本是：

```text
读一帧
处理一帧
再读下一帧
```

线程池版本是：

```text
主线程不断提交图片
多个 worker 线程并行做推理
主线程按帧号取回结果
```

注意：这个工程里每个线程都会创建自己的 `Yolov8Custom` 实例，也就是每个线程各自加载一份模型。这样写比较简单，也避免多个线程抢同一个模型对象，但会占用更多内存。

## 5. 整体工作流程

这个工程最核心的一条线是：

```text
main
  -> Yolov8Custom::LoadModel()
  -> Yolov8Custom::Run()
      -> Preprocess()
      -> Inference()
      -> Postprocess()
      -> letterbox_decode()
  -> DrawDetections()
```

展开后是：

```text
图片文件
  -> OpenCV 读取成 cv::Mat
  -> letterbox 补边
  -> BGR 转 RGB
  -> resize 到模型输入尺寸
  -> 拷贝到 input_tensor_
  -> rknn_inputs_set()
  -> rknn_run()
  -> rknn_outputs_get()
  -> YOLOv8 后处理
  -> NMS 去重
  -> 坐标还原
  -> OpenCV 画框
  -> 保存结果图片
```

下面逐段讲。

## 6. 第一站：OpenCV 读图片

在 `src/yolov8_img.cpp` 里：

```cpp
cv::Mat img = cv::imread(img_file);
```

`cv::Mat` 可以理解成 OpenCV 里面表示图片的对象。

一张彩色图片本质上是一堆数字：

```text
宽 x 高 x 3 个颜色通道
```

OpenCV 默认读出来的颜色顺序是 BGR：

```text
Blue
Green
Red
```

但很多模型习惯 RGB：

```text
Red
Green
Blue
```

所以后面前处理会做 BGR 到 RGB 的转换。

## 7. 第二站：加载模型

在入口里调用：

```cpp
yolo.LoadModel(model_file);
```

实际实现位于：

```text
src/task/yolov8_custom.cpp
```

`LoadModel()` 主要做：

```text
调用 RKNN 引擎加载 .rknn 文件
获取模型输入信息
获取模型输出信息
给输入 buffer 分配内存
给输出 buffer 分配内存
记录量化参数 scale 和 zero point
```

### 7.1 什么是模型输入输出信息

模型不是随便接收任何图片的。它会要求固定格式，比如：

```text
1 x 640 x 640 x 3
```

意思是：

```text
1 张图片
高度 640
宽度 640
3 个颜色通道
```

所以代码要先查询模型需要什么输入尺寸，然后按照这个尺寸准备图片。

输出信息也很重要。这个工程里的 YOLOv8 后处理假设模型有 6 个输出：

```text
stride 8  的框输出
stride 8  的类别输出
stride 16 的框输出
stride 16 的类别输出
stride 32 的框输出
stride 32 的类别输出
```

如果你换了一个模型，输出数量或形状不一样，后处理就可能不能直接用。

## 8. 第三站：前处理 Preprocess

调用位置：

```cpp
Preprocess(img, "opencv", image_letterbox);
```

前处理的作用是：把普通图片变成模型能吃的输入。

它主要做三件事。

### 8.1 letterbox

YOLOv8 通常输入是正方形，比如：

```text
640 x 640
```

但真实图片可能是：

```text
1280 x 720
1920 x 1080
640 x 480
```

如果直接强行拉伸到 `640 x 640`，图像会变形，检测框可能不准。

所以常用做法是：

```text
保持原始宽高比
不足的地方补黑边
```

这就叫 letterbox。

例如一张宽图：

```text
原图：
宽很多，高较少

处理后：
上下补黑边，变成接近模型需要的比例
```

代码在：

```text
src/process/preprocess.cpp
letterbox()
```

### 8.2 BGR 转 RGB

OpenCV 读图是 BGR，模型一般需要 RGB。

代码里用：

```cpp
cv::cvtColor(img, img_rgb, cv::COLOR_BGR2RGB);
```

### 8.3 resize 并拷贝到输入张量

模型输入尺寸固定，所以要 resize。

然后把图像数据复制到：

```cpp
input_tensor_.data
```

这里的 `input_tensor_` 可以理解成“模型输入数据包”。

## 9. 什么是张量 tensor

在这个项目里你会经常看到：

```text
tensor
input_tensor_
output_tensors_
tensor_data_s
tensor_attr_s
```

张量可以先简单理解成：

```text
多维数组
```

一张彩色图片就是一个三维数组：

```text
高度 x 宽度 x 颜色通道
```

一批图片就是四维数组：

```text
图片数量 x 高度 x 宽度 x 颜色通道
```

在模型部署里，张量还要包含这些信息：

```text
数据地址
数据类型
维度数量
每个维度大小
总字节数
量化参数
```

所以 `tensor_data_s` 这类结构就是用来描述“这一块模型数据是什么”的。

## 10. 第四站：NPU 推理 Inference

前处理完成后，调用：

```cpp
Inference();
```

它内部会调用：

```cpp
engine_->Run(inputs, output_tensors_, want_float_);
```

真正的 RKNN 推理在：

```text
src/engine/rknn_engine.cpp
RKEngine::Run()
```

流程是：

```text
把 input_tensor_ 转成 rknn_input
调用 rknn_inputs_set()
调用 rknn_run()
调用 rknn_outputs_get()
把结果拷贝回 output_tensors_
```

可以把它理解成：

```text
把准备好的图片数据交给 NPU
NPU 跑完模型
取回模型输出
```

这里的 NPU 是专门用来跑神经网络的硬件，比 CPU 更适合做这类计算。

## 11. 第五站：YOLOv8 后处理 Postprocess

模型输出不是直接的人类可读检测框。

模型输出通常是很多数字，比如：

```text
某个网格位置属于人的概率
某个网格位置属于车的概率
目标框左边距离
目标框上边距离
目标框右边距离
目标框下边距离
```

后处理就是把这些数字翻译成：

```text
类别：person
置信度：0.86
框位置：x, y, width, height
```

后处理代码在：

```text
src/process/postprocess.cpp
```

### 11.1 检测头

这个工程里写死了 3 个检测头：

```cpp
static int headNum = 3;
static int strides[3] = {8, 16, 32};
static int mapSize[3][2] = {{80, 80}, {40, 40}, {20, 20}};
```

可以这样理解：

```text
80 x 80   负责检测较小目标
40 x 40   负责检测中等目标
20 x 20   负责检测较大目标
```

每个网格位置都会预测一些候选框。

### 11.2 类别分数

代码会遍历每个类别：

```cpp
for (int cl = 0; cl < class_num; cl++)
```

找到分数最高的类别。

当前工程写死：

```cpp
static int class_num = 5;
```

对应类别在 `src/task/yolov8_custom.cpp`：

```cpp
"pedestrians"
"riders"
"partially-visible-person"
"ignore-regions"
"crowd"
```

所以这个工程当前不是完整 COCO 80 类后处理，而是 5 类版本。

如果你换成 80 类 YOLOv8 模型，这里也要改。

### 11.3 量化和反量化

这个工程支持 int8 量化模型。

int8 模型输出不是直接的浮点数，而是压缩过的整数。为了得到真实数值，需要反量化：

```cpp
float value = (qnt - zp) * scale;
```

其中：

```text
qnt    int8 原始值
zp     zero point
scale  缩放系数
```

工程里对应函数是：

```cpp
DeQnt2F32()
```

如果是 float 模型，则走另一套 float 后处理。

### 11.4 sigmoid

模型输出的类别分数通常还不是 0 到 1 的概率。

代码会用：

```cpp
sigmoid()
```

把它转换到 0 到 1 之间。

比如：

```text
0.86 表示模型认为这个框是某类目标的置信度较高
```

### 11.5 阈值过滤

代码里有：

```cpp
static float objectThreshold = 0.2;
```

意思是：

```text
如果候选框分数小于 0.2，就丢掉
```

这样可以减少大量低质量框。

### 11.6 NMS

同一个目标附近可能会预测出很多框。

比如一个人，模型可能预测出：

```text
框 A：0.91
框 B：0.87
框 C：0.73
```

这些框位置很接近，实际上都是同一个人。

NMS 的作用是：

```text
保留分数最高的框
删除和它重叠太多的框
```

代码里的阈值是：

```cpp
static float nmsThreshold = 0.25;
```

## 12. 第六站：坐标还原

前处理做了 letterbox，也就是补边。

所以模型预测出来的框坐标是在“补边后的图片”上，不完全等于原图坐标。

代码最后调用：

```cpp
letterbox_decode(objects, letterbox_info_.hor, letterbox_info_.pad);
```

它会把补边造成的偏移减掉：

```text
如果左右补边，就修正 x
如果上下补边，就修正 y
```

不过这个工程里的坐标还原比较简单。如果要做生产级项目，建议重点检查这里，确认输出框和原图完全对齐。

## 13. 第七站：画框

画框代码在：

```text
src/draw/cv_draw.cpp
```

入口里调用：

```cpp
DrawDetections(img, objects);
```

它会遍历所有检测结果：

```text
画矩形框
写类别名
写置信度
```

图片版本最后保存：

```cpp
cv::imwrite("result.jpg", img);
```

## 14. 数据在项目里怎么流动

可以用这个图理解：

```text
cv::Mat img
  |
  | Preprocess
  v
input_tensor_.data
  |
  | RKNN Engine
  v
output_tensors_
  |
  | Postprocess
  v
std::vector<Detection> objects
  |
  | DrawDetections
  v
result.jpg / result.mp4
```

每个变量可以这样理解：

```text
img
  OpenCV 读出来的图片

input_tensor_
  模型输入张量，里面放处理后的图片数据

output_tensors_
  模型输出张量，里面放 NPU 推理后的原始结果

objects
  后处理后的检测框，已经适合画图或业务使用
```

## 15. 几个关键文件应该怎么读

推荐阅读顺序：

### 15.1 第一步：看图片入口

```text
src/yolov8_img.cpp
```

这个文件最短，能看到完整主流程。

重点看：

```cpp
yolo.LoadModel(model_file);
yolo.Run(img, objects);
DrawDetections(img, objects);
```

### 15.2 第二步：看 Yolov8Custom

```text
src/task/yolov8_custom.h
src/task/yolov8_custom.cpp
```

这是整个项目的核心调度层。

重点看：

```text
LoadModel()
Run()
Preprocess()
Inference()
Postprocess()
```

### 15.3 第三步：看 RKNN 引擎

```text
src/engine/engine.h
src/engine/rknn_engine.h
src/engine/rknn_engine.cpp
```

这里负责和 Rockchip RKNN Runtime 交互。

重点看：

```text
rknn_init()
rknn_query()
rknn_inputs_set()
rknn_run()
rknn_outputs_get()
rknn_destroy()
```

### 15.4 第四步：看前处理

```text
src/process/preprocess.cpp
```

重点看：

```text
letterbox()
cvimg2tensor()
cvimg2tensor_rga()
```

### 15.5 第五步：看后处理

```text
src/process/postprocess.cpp
```

重点看：

```text
GetConvDetectionResultInt8()
GetConvDetectionResult()
GenerateMeshgrid()
IOU()
NMS 逻辑
```

后处理是最难的部分，但也是理解 YOLO 部署最关键的部分。

## 16. CMakeLists.txt 是做什么的

`CMakeLists.txt` 是 C++ 工程的构建配置文件。

它告诉编译工具：

```text
要编译哪些 .cpp 文件
要生成哪些库
要生成哪些可执行程序
要链接哪些第三方库
```

这个工程会生成这些库：

```text
nn_process
  前处理和后处理

rknn_engine
  RKNN 推理引擎

yolov8_lib
  YOLOv8 任务封装

draw_lib
  画框
```

也会生成这些可执行程序：

```text
yolov8_img
yolov8_video
yolov8_thread_pool
```

## 17. 这个工程默认偏 RK3588

从 `CMakeLists.txt` 可以看到：

```cmake
set(LIB_ARCH "aarch64")
set(LIB_ARCH_RGA "gcc-aarch64")
set(DEVICE_NAME "RK3588")
```

所以它默认是 RK3588 / aarch64 方向。

如果要迁移到别的 Rockchip 芯片，比如 RV1126，需要重点确认：

```text
RKNN runtime 是否匹配
rknn_api.h 是否匹配
librknnrt.so / librknn_api.so 是否匹配
RGA 库架构是否匹配
模型是否由对应工具链转换
```

这类工程里，模型和 runtime 不匹配是非常常见的问题。

## 18. 换模型时最容易出问题的地方

如果你换了自己的 `.rknn` 模型，重点检查这些地方。

### 18.1 输入尺寸

后处理里写死：

```cpp
static int input_w = 640;
static int input_h = 640;
```

如果模型不是 640 x 640，要改。

### 18.2 类别数量

后处理里写死：

```cpp
static int class_num = 5;
```

如果你的模型是 2 类、3 类、80 类，要改。

### 18.3 类别名字

`src/task/yolov8_custom.cpp` 里写死了类别名：

```cpp
static std::vector<std::string> g_classes = {
    "pedestrians",
    "riders",
    "partially-visible-person",
    "ignore-regions",
    "crowd"
};
```

类别数量和类别名必须对应。

### 18.4 输出 tensor 数量

`LoadModel()` 里检查：

```cpp
if (output_shapes.size() != 6)
```

如果你的 YOLOv8 模型导出方式不同，可能不是 6 个输出。

### 18.5 后处理格式

YOLOv8 的输出格式和 YOLOv5 不完全一样。

不能看到都是 YOLO 就直接复用后处理。你必须确认：

```text
输出 tensor 的数量
每个 tensor 的 shape
reg 和 cls 的排列方式
是否已经包含 sigmoid
是否是 int8 量化输出
```

## 19. 线程池版本怎么理解

线程池代码在：

```text
src/task/yolov8_thread_pool.cpp
```

它做了三件事：

```text
创建多个 Yolov8Custom 实例
创建多个 worker 线程
把图片任务分发给这些线程处理
```

核心流程：

```text
setUp()
  -> 创建模型实例
  -> 启动线程

submitTask()
  -> 提交一张图片和帧号

worker()
  -> 等待任务
  -> 取出图片
  -> 调用 instance->Run()
  -> 保存结果

getTargetImgResult()
  -> 根据帧号取回结果图片
```

线程池版本适合性能更强的平台。对资源较小的平台，未必越多线程越快，因为模型实例越多，内存占用也越高。

## 20. 一句话总结

这个工程可以这样记：

```text
yolov8_img.cpp 是入口
Yolov8Custom 是总调度
preprocess.cpp 把图片变成模型输入
rknn_engine.cpp 负责调用 NPU
postprocess.cpp 把模型输出变成检测框
cv_draw.cpp 把检测框画回图片
```

第一次读代码，不要一上来钻后处理公式。建议先把主流程走通：

```text
图片从哪里来
模型在哪里加载
输入 tensor 在哪里准备
NPU 在哪里运行
输出 tensor 在哪里解析
检测框在哪里画出来
```

把这条线串起来后，再深入理解 YOLOv8 的解码、量化、NMS，会轻松很多。
