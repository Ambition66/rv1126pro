# 嵌入式AI

## 基础篇
1. 快速了解RK3588
2. 什么是NPU？与CPU/GPU的区别
   - NPU/CPU/GPU是什么
   - NPU算力计算方法
3. RKNPU推理软件架构与模型部署流程
   - 推理软件架构
   - AI模型部署流程
   - rknn开发流程介绍
4. 瑞芯微原厂NPU资料介绍
   - github仓库介绍（rknn-toolkit2、rknn_model_zoo、rknn_llm、librga）
   - rknn-toolkit2介绍
   - rknn-toolkit-lite2介绍
   - rknpu2介绍
5. 搭建 RKNN Toolkit2 开发环境
   - 安装vmware和ubuntu20
   - 安装miniconda
   - 创建RKNN-Toolkit2 Conda环境
   - 安装pycharm
   - 安装vs code
6. 设备端NPU环境准备
   - NPU驱动版本确认
   - NPU连板环境确认
   - 更新 npu 连板推理运行库
7. 查看和调整CPU和NPU频率
   - 查看和固定CPU频率
   - 查看和固定NPU频率
   - 快速定频
8. 快速体验rknn_demo
   - rknn-toolkit2 demo体验
   - rknn-toolkit-lite2 demo体验
   - rknpu2 demo体验
9. RKNN模型转换
   - 模型转换流程
   - RKNN初始化及对象释放
   - 模型转换配置
   - 模型加载接口
   - 构建RKNN模型
   - 导出RKNN模型
10. 均值和标准差
    - rknn.config()的标准化参数
    - 标准化和归一化
    - 预训练模型的参数值
11. 模型转换工具rknn_convert
12. rknn-toolkit2模型推理
    - 模拟器推理
    - 连板推理
13. 模型评估—模型量化精度分析
    - 量化的基本介绍
    - 常规量化
    - 混合量化流程
    - 量化结果分析（余弦距离和欧式距离）
14. 模型评估—模型性能评估
    - 性能评估流程
    - 性能评估API介绍
    - 最简单的一种性能优化方式
15. 模型评估—模型内存评估
    - 模型内存评估流程
    - 实操
16. 板端Python API推理—RKNN-ToolkitLite2
    - 基本使用流程
    - API介绍
    - 源码分析（Python）
17. 板端C API推理—RKNPU2
    - 通用C API调用流程
    - 零拷贝C API调用流程

## 实战篇
1. yolov5模型部署实战（单线程和多线程板）
2. yolov8模型部署实战（姿态识别带推拉流）
3. yolov13模型部署实战（非官方模型的部署）
4. yolov26模型部署实战（从0到1实现）
5. qwen部署实战
6. deepseek部署实战

## 进阶篇
1. NPU多核配置详解
   - 多核运行配置方法
   - 查看多核运行效果
   - 多核性能提升技巧
2. 数据排列格式
   - NCHW
   - NWHC
   - NC1HWC2
3. 多Batch使用说明
   - 多Batch原理
   - 多Batch使用方法
   - 多Batch输入输出设置
4. 模型剪枝与加密
   - 模型剪枝
   - 模型加密
5. Cacheable内存一致性
   - Cacheable内存同步的方向
   - 同步Cacheable内存
6. 动态shape
   - 动态shape概念介绍
   - RKNN SDK版本和平台要求
   - 生成动态shape的RKNN模型
7. 自动生成部署C代码
8. 量化说明
   - 量化介绍
   - 量化配置
   - 混合量化
9. 精度排查
   - 模拟器精度排查
   - Runtime精度排查
10. 性能优化
    - CPU、NPU、DDR定频
    - 模型性能分析
    - 量化加速
    - 图级别优化
    - 算子级别优化
11. 内存优化
    - 使用外部分配内存
    - Internal内存复用
    - 多线程复用上下文
    - 多种分辨率模型共享相同权重