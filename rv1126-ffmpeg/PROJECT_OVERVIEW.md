# rv1126-ffmpeg 工程说明

本文档根据工程源码、Makefile、头文件和依赖目录整理，未参考工程内已有 Markdown 文档。

## 项目定位

本工程是面向 Rockchip RV1126/RV1109 平台的视频采集与网络推流程序。程序通过 RKMedia 初始化摄像头输入、硬件视频编码和 RGA 图像处理，再用 FFmpeg 将编码后的 H.264 码流封装为 FLV 或 MPEG-TS，推送到外部网络地址。

当前主程序实现的是双码流视频推送：

- 主码流：1920x1080，H.264 CBR，25 fps。
- 子码流：VI 输入经 RGA 输出为 1280x720，再送入第二路 H.264 编码器，25 fps。
- 输出封装：`0` 表示 FLV，常用于 RTMP；`1` 表示 MPEG-TS，代码注释中用于 SRT/UDP/RTSP 一类地址。
- 音频相关结构和 AENC 初始化函数存在，但当前主流程没有真正启用音频推流。

## 运行入口

入口文件是 `rv1126_ffmpeg_main.cpp`。程序要求 4 个参数：

```bash
./rv1126_ffmpeg_main high_stream_type high_url_address low_stream_type low_url_address
```

主函数做的事情很少：

1. 解析主码流协议类型和地址。
2. 解析子码流协议类型和地址。
3. 创建全局 `high_video_queue` 和 `low_video_queue`。
4. 调用 `init_rkmedia_module_function()` 初始化 RKMedia 相关模块。
5. 调用 `init_rv1126_first_assignment()` 建立绑定关系并启动线程。
6. 主线程进入常驻循环。

## 构建方式

工程使用根目录 `Makefile` 构建，交叉编译器路径写死为 RV1126/RV1109 SDK 内的：

```makefile
/opt/rv1126_rv1109_linux_sdk_v1.8.0_20210224/prebuilts/gcc/linux-x86/arm/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/bin/arm-linux-gnueabihf-g++
```

构建目标为：

```text
rv1126_ffmpeg_main
```

Makefile 将根目录下主要 `.cpp/.c` 文件一次性编译链接，依赖包括：

- RKMedia / Easymedia / MPP / DRM / RGA。
- FFmpeg：`avformat`、`avcodec`、`swresample`、`avutil`。
- ALSA、V4L2。
- x264、SRT、OpenSSL。
- SDL、SDL_ttf、Freetype。
- RKAIQ 和 ISP 相关库。

## 目录结构

根目录下的源码是业务逻辑主体：

- `rv1126_ffmpeg_main.cpp`：程序入口。
- `rkmedia_module_function.cpp`：配置 VI、VENC、RGA、低码流 VENC 等 RKMedia 模块。
- `rkmedia_module.cpp`：对 RKMedia 的 VI、AI、VENC、AENC 初始化函数做薄封装。
- `rkmedia_assignment_manage.cpp`：建立模块绑定关系，初始化 FFmpeg 输出，创建采集和推流线程。
- `rkmedia_data_process.cpp`：采集 VENC/RGA 输出、写入队列、转换为 FFmpeg `AVPacket` 并推流。
- `rkmedia_ffmpeg_config.cpp`：创建 FFmpeg 输出上下文、配置流、打开输出地址、写 header、释放资源。
- `ffmpeg_video_queue.cpp`：视频包生产消费队列。
- `ffmpeg_audio_queue.cpp`：音频包生产消费队列，当前主流程未使用。
- `rkmedia_container.cpp`：保存 VI/AI/VENC/AENC 通道 ID 的全局容器。
- `rv1126_isp_function.cpp`、`sample_common_isp.c`：ISP 初始化与控制辅助函数。

依赖和预编译资源目录：

- `include/`：RKMedia、RKAIQ、Easymedia 等头文件。
- `rv1126_lib/`：RV1126 目标板运行需要的 `.so` 库，包含 RKMedia、RGA、RKAIQ、MPP、V4L2、ALSA、FFmpeg、RKNPU/RockX 等。
- `arm32_ffmpeg_srt/`：ARM 版本 FFmpeg/SRT 相关头文件、库和工具。
- `arm_libx264/`、`arm_freetype/`、`arm_sdl/`、`arm_sdl_ttf_install/`：交叉编译后的第三方库。
- `opt/`：额外 ARM 依赖库目录，Makefile 中链接了 `arm_libx264`、`arm32_ffmpeg_srt`、`arm_libsrt`、`arm_openssl` 等路径。

## 数据流

整体视频链路如下：

```text
摄像头 VI(1920x1080 NV12)
        |
        +--> VENC0(H.264 1080p) --> high_video_queue --> FFmpeg 封装 --> 主码流 URL
        |
        +--> RGA(缩放/翻转到 1280x720 NV12) --> VENC1(H.264 720p) --> low_video_queue --> FFmpeg 封装 --> 子码流 URL
```

其中：

- `RK_MPI_SYS_Bind(VI, VENC0)` 建立主码流硬件链路。
- `RK_MPI_SYS_Bind(VI, RGA)` 建立子码流前处理链路。
- `get_rga_thread()` 从 RGA 通道取出图像，再通过 `RK_MPI_SYS_SendMediaBuffer(RK_ID_VENC, 1, mb)` 送入 VENC1。
- `camera_venc_thread()` 从 VENC0 拉取编码后的 H.264 数据，放入 `high_video_queue`。
- `low_camera_venc_thread()` 从 VENC1 拉取编码后的 H.264 数据，放入 `low_video_queue`。
- `high_video_push_thread()` 和 `low_video_push_thread()` 从队列取包，写入 FFmpeg 输出上下文。

## RKMedia 模块配置

`init_rkmedia_module_function()` 是硬件通道配置的核心：

- 调用 `RK_MPI_SYS_Init()` 初始化 RKMedia 系统。
- 创建 VI0：
  - 节点：`rkispp_scale0`。
  - 分辨率：1920x1080。
  - 格式：NV12。
  - 缓冲数：3。
- 创建 VENC0：
  - 编码：H.264。
  - 分辨率：1920x1080。
  - Profile：66，也就是 Baseline。
  - 码率控制：H.264 CBR。
  - GOP：25。
  - 目标帧率：25 fps。
- 创建 RGA0：
  - 输入：1920x1080 NV12。
  - 输出：1280x720 NV12。
  - `enFlip = RGA_FLIP_H`，开启水平翻转。
  - 缓冲池数量：3。
- 创建 VENC1：
  - 编码：H.264。
  - 分辨率：1280x720。
  - Profile：66。
  - 码率控制：H.264 CBR。
  - GOP：30。
  - 目标帧率：25 fps。

## FFmpeg 封装逻辑

FFmpeg 相关逻辑集中在 `rkmedia_ffmpeg_config.cpp` 和 `rkmedia_data_process.cpp`。

`init_rkmedia_ffmpeg_context()` 根据协议类型创建输出上下文：

- `FLV_PROTOCOL`：调用 `avformat_alloc_output_context2(..., "flv", network_addr)`。
- `TS_PROTOCOL`：调用 `avformat_alloc_output_context2(..., "mpegts", network_addr)`。

视频流配置为：

- 编码 ID：调用方传入，目前使用 `AV_CODEC_ID_H264`。
- 分辨率：主码流 1920x1080，子码流 1280x720。
- 像素格式：`AV_PIX_FMT_NV12`。
- 时间基：`1/25`。
- GOP：25。
- 码率：`width * height * 3`。

推流线程中不会重新编码原始图像，而是把 RKMedia VENC 已经编码好的 H.264 数据拷贝进 FFmpeg `AVPacket`，设置递增 PTS，然后调用 `av_interleaved_write_frame()` 写入封装器。

## 线程模型

`init_rv1126_first_assignment()` 启动以下线程：

- `camera_venc_thread`：读取 VENC0 主码流编码数据。
- `get_rga_thread`：读取 RGA 输出，并送入 VENC1。
- `low_camera_venc_thread`：读取 VENC1 子码流编码数据。
- `high_video_push_thread`：主码流 FFmpeg 推流。
- `low_video_push_thread`：子码流 FFmpeg 推流。

这些线程都采用无限循环模型，采集线程阻塞等待 RKMedia buffer，推流线程阻塞等待队列数据。线程创建后使用 `pthread_detach()` 分离，主线程只负责保持进程存活。

## 队列设计

视频队列类型为 `VIDEO_QUEUE`：

- 内部使用 `std::queue<video_data_packet_t *>`。
- 用 `pthread_mutex_t` 保护队列。
- 用 `pthread_cond_t` 在空队列时阻塞消费者。
- 每个 `video_data_packet_t` 内置最大 3 MB 的固定缓冲区。

音频队列 `AUDIO_QUEUE` 结构类似，单包最大也是 3 MB，但当前主链路没有启用 AI/AENC 采集和音频写入。

## ISP 支持

`rv1126_isp_function.cpp` 提供 `init_all_isp_function()`：

- 初始化 RKAIQ。
- 启动 ISP。
- 设置帧率为 30 fps。

不过主入口当前没有调用该函数，说明 ISP 初始化可能由外部流程完成，或者该功能尚未接入主流程。

## 当前代码中的注意点

以下是直接从代码行为观察到的点，适合后续维护时优先确认：

- `init_rkmedia_ffmpeg_function()` 在 `main()` 中被注释，当前也没有看到全局 FFmpeg 配置表被实际使用。
- `init_rv1126_first_assignment()` 的 `low_url_type` 参数没有用于子码流配置，代码仍把 `protocol_type` 赋给 `low_ffmpeg_config->protocol_type`。
- `low_venc_channel.s32ChnId` 在赋值给 `low_venc_arg_params->vencId` 前没有初始化；不过 `low_camera_venc_thread()` 内部实际固定读取 VENC 通道 `1`，所以该参数目前没有生效。
- FFmpeg 输出只启用了视频流，音频流创建代码被 `#if 0` 关闭，但释放时仍调用了 `free_stream(..., &audio_stream)`，需要确认空指针路径是否安全。
- 队列没有容量上限，网络阻塞或推流失败时可能导致内存持续增长。
- H.264 包写入 FFmpeg 时统一设置了 `AV_PKT_FLAG_KEY`，没有使用 RKMedia buffer 的真实帧类型标记。
- OSD、SDL_ttf、BMP/文字叠加相关代码大部分被注释或条件编译关闭，属于未启用功能。

## 适合的维护方向

如果要继续完善这个工程，优先级建议如下：

1. 修正子码流协议类型参数使用和 `low_venc_channel` 未初始化问题。
2. 明确音频是否需要支持；如果暂不支持，应移除或保护未初始化 audio stream 的释放逻辑。
3. 给视频队列增加容量限制或丢帧策略，避免推流端阻塞时内存无界增长。
4. 使用真实 NALU/帧类型设置 `AVPacket` flags，避免所有包都被标成关键帧。
5. 将分辨率、码率、GOP、帧率、VI 节点、RGA 翻转、协议类型等硬编码参数抽成配置。
6. 明确 ISP 初始化由主程序负责还是外部负责，避免不同部署环境下启动行为不一致。
