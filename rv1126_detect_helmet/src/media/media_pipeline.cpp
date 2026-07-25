#include "media_pipeline.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <algorithm>
#include <atomic>
#include <queue>

#ifdef ENABLE_RKMEDIA
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

#include "rkmedia_api.h"
#include "rkmedia_buffer.h"
#include "rkmedia_rga.h"
#include "rkmedia_venc.h"
#include "rkmedia_vi.h"

#define CAMERA_ID 0
#define VI_CHN_ID 0
#define VENC_CHN_ID 0
#define RGA_CHN_ID 0
#define CMOS_DEVICE_NAME "rkispp_scale0"

// VENC 输出的是已经编码好的 H264 包，FFmpeg 线程只负责封装和推流。
struct EncodedPacket {
    unsigned char *data;
    int size;
    int key_frame;
};

// 内部 H264 包队列：VENC 线程生产，FFmpeg 推流线程消费。
// 和 AI 帧队列不同，这里不能只保留最新包，否则视频流会断帧。
class EncodedPacketQueue {
public:
    EncodedPacketQueue() : closed_(false) {
        pthread_mutex_init(&mutex_, NULL);
        pthread_cond_init(&cond_, NULL);
    }

    ~EncodedPacketQueue() {
        Close();
        pthread_mutex_destroy(&mutex_);
        pthread_cond_destroy(&cond_);
    }

    int Push(const void *data, int size, int key_frame) {
        if (!data || size <= 0) {
            return -1;
        }

        // VENC 的 MEDIA_BUFFER 释放后原始指针失效，因此这里复制一份编码包。
        EncodedPacket *packet = (EncodedPacket *)malloc(sizeof(EncodedPacket));
        if (!packet) {
            return -2;
        }
        packet->data = (unsigned char *)malloc(size);
        if (!packet->data) {
            free(packet);
            return -2;
        }
        memcpy(packet->data, data, size);
        packet->size = size;
        packet->key_frame = key_frame;

        pthread_mutex_lock(&mutex_);
        if (closed_) {
            pthread_mutex_unlock(&mutex_);
            free(packet->data);
            free(packet);
            return -3;
        }
        queue_.push(packet);
        pthread_cond_signal(&cond_);
        pthread_mutex_unlock(&mutex_);
        return 0;
    }

    EncodedPacket *Pop() {
        pthread_mutex_lock(&mutex_);
        while (!closed_ && queue_.empty()) {
            pthread_cond_wait(&cond_, &mutex_);
        }
        if (closed_) {
            pthread_mutex_unlock(&mutex_);
            return NULL;
        }
        EncodedPacket *packet = queue_.front();
        queue_.pop();
        pthread_mutex_unlock(&mutex_);
        return packet;
    }

    void Close() {
        pthread_mutex_lock(&mutex_);
        closed_ = true;
        while (!queue_.empty()) {
            EncodedPacket *packet = queue_.front();
            queue_.pop();
            free(packet->data);
            free(packet);
        }
        pthread_cond_broadcast(&cond_);
        pthread_mutex_unlock(&mutex_);
    }

private:
    pthread_mutex_t mutex_;
    pthread_cond_t cond_;
    std::queue<EncodedPacket *> queue_;
    bool closed_;
};

class FfmpegWriter {
public:
    FfmpegWriter()
        : oc_(NULL),
          video_stream_(NULL),
          next_pts_(0),
          fps_(25) {}

    ~FfmpegWriter() {
        Close();
    }

    int Open(const char *url, int protocol, int width, int height, int fps) {
        if (!url) {
            return -1;
        }

        fps_ = fps > 0 ? fps : 25;
        avformat_network_init();

        // protocol=0 使用 flv，适合 RTMP；protocol=1 使用 mpegts，适合 TS/SRT 类场景。
        const char *format = protocol == 1 ? "mpegts" : "flv";
        int ret = avformat_alloc_output_context2(&oc_, NULL, format, url);
        if (ret < 0 || !oc_) {
            printf("avformat_alloc_output_context2 failed: %d\n", ret);
            return -1;
        }

        video_stream_ = avformat_new_stream(oc_, NULL);
        if (!video_stream_) {
            printf("avformat_new_stream failed\n");
            return -1;
        }
        video_stream_->id = oc_->nb_streams - 1;
        video_stream_->time_base = (AVRational){1, fps_};
        video_stream_->r_frame_rate = (AVRational){fps_, 1};

        // 这里不调用软件编码器。RV1126 VENC 已经输出 H264，FFmpeg 只需要知道流参数。
        video_stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        video_stream_->codecpar->codec_id = AV_CODEC_ID_H264;
        video_stream_->codecpar->width = width;
        video_stream_->codecpar->height = height;
        video_stream_->codecpar->format = AV_PIX_FMT_NV12;
        video_stream_->codecpar->bit_rate = width * height * 3;

        av_dump_format(oc_, 0, url, 1);
        if (!(oc_->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&oc_->pb, url, AVIO_FLAG_WRITE);
            if (ret < 0) {
                printf("avio_open failed: %d\n", ret);
                return -1;
            }
        }

        ret = avformat_write_header(oc_, NULL);
        if (ret < 0) {
            printf("avformat_write_header failed: %d\n", ret);
            return -1;
        }
        return 0;
    }

    int Write(const EncodedPacket &packet) {
        if (!oc_ || !video_stream_) {
            return -1;
        }

        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data = packet.data;
        pkt.size = packet.size;
        pkt.stream_index = video_stream_->index;
        pkt.pts = next_pts_;
        pkt.dts = next_pts_;
        pkt.duration = 1;
        if (packet.key_frame) {
            pkt.flags |= AV_PKT_FLAG_KEY;
        }
        next_pts_++;

        // VENC 包按帧顺序写入，PTS 以 fps 为时间基递增。
        av_packet_rescale_ts(&pkt, (AVRational){1, fps_}, video_stream_->time_base);
        return av_interleaved_write_frame(oc_, &pkt);
    }

    void Close() {
        if (!oc_) {
            return;
        }
        av_write_trailer(oc_);
        if (!(oc_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&oc_->pb);
        }
        avformat_free_context(oc_);
        oc_ = NULL;
        video_stream_ = NULL;
        next_pts_ = 0;
    }

private:
    AVFormatContext *oc_;
    AVStream *video_stream_;
    int64_t next_pts_;
    int fps_;
};

class MediaPipelineImpl {
public:
    MediaPipelineImpl()
        : ai_queue_(NULL),
          result_manager_(NULL),
          running_(false),
          rkmedia_ready_(false),
          vi_ready_(false),
          venc_ready_(false),
          rga_ready_(false),
          bind_venc_ready_(false),
          bind_rga_ready_(false),
          venc_thread_(0),
          push_thread_(0),
          ai_thread_(0),
          frame_id_(0),
          ai_schedule_accumulator_(0),
          rga_width_(0),
          rga_height_(0) {
        memset(&config_, 0, sizeof(config_));
    }

    int Init(const media_pipeline_config_t &config,
             AiFrameQueue *ai_queue,
             AiResultManager *result_manager) {
        config_ = config;
        ai_queue_ = ai_queue;
        result_manager_ = result_manager;
        if (!ai_queue_ || !result_manager_ || !config_.stream_url) {
            return -1;
        }
        if (config_.ai_width <= 0) {
            config_.ai_width = 640;
        }
        if (config_.ai_height <= 0) {
            config_.ai_height = 640;
        }
        // Use an accumulator so non-divisible rates such as 25 -> 15 FPS are
        // scheduled accurately instead of degenerating to every frame.
        ai_schedule_accumulator_ = 0;

        // Fit the camera image inside the requested model canvas without
        // stretching. HelmetDetector adds the remaining letterbox padding.
        rga_width_ = config_.ai_width;
        rga_height_ = config_.ai_height;
        if ((int64_t)config_.width * config_.ai_height >
            (int64_t)config_.height * config_.ai_width) {
            rga_height_ = (int)((int64_t)config_.height * config_.ai_width /
                                config_.width);
        } else {
            rga_width_ = (int)((int64_t)config_.width * config_.ai_height /
                               config_.height);
        }
        rga_width_ = std::max(2, rga_width_ & ~1);
        rga_height_ = std::max(2, rga_height_ & ~1);

        int ret = InitRkmedia();
        if (ret != 0) {
            return ret;
        }

        ret = writer_.Open(config_.stream_url,
                           config_.stream_protocol,
                           config_.width,
                           config_.height,
                           config_.fps);
        if (ret != 0) {
            ReleaseRkmedia();
            return ret;
        }
        return 0;
    }

    int Start() {
        if (running_) {
            return 0;
        }
        running_ = true;

        // 三个线程分别处理编码取包、网络推流、AI 抽帧，互不阻塞。
        int ret = pthread_create(&venc_thread_, NULL, VencThreadEntry, this);
        if (ret != 0) {
            running_ = false;
            return ret;
        }
        ret = pthread_create(&push_thread_, NULL, PushThreadEntry, this);
        if (ret != 0) {
            running_ = false;
            return ret;
        }
        ret = pthread_create(&ai_thread_, NULL, AiFrameThreadEntry, this);
        if (ret != 0) {
            running_ = false;
            return ret;
        }
        return 0;
    }

    void Stop() {
        if (!running_ && !rkmedia_ready_) {
            return;
        }

        running_ = false;
        packet_queue_.Close();

        if (venc_thread_) {
            pthread_join(venc_thread_, NULL);
            venc_thread_ = 0;
        }
        if (push_thread_) {
            pthread_join(push_thread_, NULL);
            push_thread_ = 0;
        }
        if (ai_thread_) {
            pthread_join(ai_thread_, NULL);
            ai_thread_ = 0;
        }

        writer_.Close();
        ReleaseRkmedia();
    }

private:
    int InitRkmedia() {
        int ret = RK_MPI_SYS_Init();
        if (ret != 0) {
            printf("RK_MPI_SYS_Init failed: %d\n", ret);
            return -1;
        }

        // VI 是摄像头入口，这里的节点需要按实际板子调整。
        VI_CHN_ATTR_S vi_attr;
        memset(&vi_attr, 0, sizeof(vi_attr));
        vi_attr.pcVideoNode = CMOS_DEVICE_NAME;
        vi_attr.u32BufCnt = 3;
        vi_attr.u32Width = config_.width;
        vi_attr.u32Height = config_.height;
        vi_attr.enPixFmt = IMAGE_TYPE_NV12;
        vi_attr.enBufType = VI_CHN_BUF_TYPE_MMAP;
        vi_attr.enWorkMode = VI_WORK_MODE_NORMAL;

        ret = RK_MPI_VI_SetChnAttr(CAMERA_ID, VI_CHN_ID, &vi_attr);
        ret |= RK_MPI_VI_EnableChn(CAMERA_ID, VI_CHN_ID);
        if (ret != 0) {
            printf("VI init failed: %d\n", ret);
            ReleaseRkmedia();
            return -1;
        }
        vi_ready_ = true;

        // 主视频链路：VI -> VENC。这里只创建一路 H264 编码通道。
        VENC_CHN_ATTR_S venc_attr;
        memset(&venc_attr, 0, sizeof(venc_attr));
        venc_attr.stVencAttr.enType = RK_CODEC_TYPE_H264;
        venc_attr.stVencAttr.imageType = IMAGE_TYPE_NV12;
        venc_attr.stVencAttr.u32PicWidth = config_.width;
        venc_attr.stVencAttr.u32PicHeight = config_.height;
        venc_attr.stVencAttr.u32VirWidth = config_.width;
        venc_attr.stVencAttr.u32VirHeight = config_.height;
        venc_attr.stVencAttr.u32Profile = 66;
        venc_attr.stVencAttr.bByFrame = RK_TRUE;
        venc_attr.stVencAttr.enRotation = VENC_ROTATION_0;
        venc_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        venc_attr.stRcAttr.stH264Cbr.u32Gop = config_.fps;
        venc_attr.stRcAttr.stH264Cbr.u32BitRate = config_.width * config_.height * 3;
        venc_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
        venc_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = config_.fps;
        venc_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        venc_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = config_.fps;

        ret = RK_MPI_VENC_CreateChn(VENC_CHN_ID, &venc_attr);
        if (ret != 0) {
            printf("VENC init failed: %d\n", ret);
            ReleaseRkmedia();
            return -1;
        }
        venc_ready_ = true;

        // AI 分支：VI -> RGA。RGA 直接输出模型友好的 RGB888 小图。
        RGA_ATTR_S rga_attr;
        memset(&rga_attr, 0, sizeof(rga_attr));
        rga_attr.stImgIn.u32Width = config_.width;
        rga_attr.stImgIn.u32Height = config_.height;
        rga_attr.stImgIn.u32HorStride = config_.width;
        rga_attr.stImgIn.u32VirStride = config_.height;
        rga_attr.stImgIn.imgType = IMAGE_TYPE_NV12;
        rga_attr.stImgOut.u32Width = rga_width_;
        rga_attr.stImgOut.u32Height = rga_height_;
        rga_attr.stImgOut.u32HorStride = rga_width_;
        rga_attr.stImgOut.u32VirStride = rga_height_;
        rga_attr.stImgOut.imgType = IMAGE_TYPE_RGB888;
        rga_attr.u16BufPoolCnt = 3;
        rga_attr.u16Rotaion = 0;
        rga_attr.enFlip = RGA_FLIP_NULL;
        rga_attr.bEnBufPool = RK_TRUE;

        ret = RK_MPI_RGA_CreateChn(RGA_CHN_ID, &rga_attr);
        if (ret != 0) {
            printf("RGA init failed: %d\n", ret);
            ReleaseRkmedia();
            return -1;
        }
        rga_ready_ = true;

        MPP_CHN_S vi_channel;
        MPP_CHN_S venc_channel;
        MPP_CHN_S rga_channel;
        memset(&vi_channel, 0, sizeof(vi_channel));
        memset(&venc_channel, 0, sizeof(venc_channel));
        memset(&rga_channel, 0, sizeof(rga_channel));
        vi_channel.enModId = RK_ID_VI;
        vi_channel.s32ChnId = VI_CHN_ID;
        venc_channel.enModId = RK_ID_VENC;
        venc_channel.s32ChnId = VENC_CHN_ID;
        rga_channel.enModId = RK_ID_RGA;
        rga_channel.s32ChnId = RGA_CHN_ID;

        // 同一个 VI 同时绑定到 VENC 和 RGA：
        // VENC 负责推流，RGA 负责 AI 抽帧，不再创建第二路 VENC 低码流。
        ret = RK_MPI_SYS_Bind(&vi_channel, &venc_channel);
        if (ret != 0) {
            printf("VI bind VENC failed: %d\n", ret);
            ReleaseRkmedia();
            return -1;
        }
        bind_venc_ready_ = true;
        ret = RK_MPI_SYS_Bind(&vi_channel, &rga_channel);
        if (ret != 0) {
            printf("VI bind RGA failed: %d\n", ret);
            ReleaseRkmedia();
            return -1;
        }
        bind_rga_ready_ = true;

        rkmedia_ready_ = true;
        printf("RKMedia ready: stream=%dx%d rga=%dx%d model_canvas=%dx%d "
               "fps=%d ai_fps=%d\n",
               config_.width,
               config_.height,
               rga_width_,
               rga_height_,
               config_.ai_width,
               config_.ai_height,
               config_.fps,
               config_.ai_fps);
        return 0;
    }

    void ReleaseRkmedia() {
        if (!rkmedia_ready_ && !vi_ready_ && !venc_ready_ && !rga_ready_) {
            return;
        }

        MPP_CHN_S vi_channel;
        MPP_CHN_S venc_channel;
        MPP_CHN_S rga_channel;
        memset(&vi_channel, 0, sizeof(vi_channel));
        memset(&venc_channel, 0, sizeof(venc_channel));
        memset(&rga_channel, 0, sizeof(rga_channel));
        vi_channel.enModId = RK_ID_VI;
        vi_channel.s32ChnId = VI_CHN_ID;
        venc_channel.enModId = RK_ID_VENC;
        venc_channel.s32ChnId = VENC_CHN_ID;
        rga_channel.enModId = RK_ID_RGA;
        rga_channel.s32ChnId = RGA_CHN_ID;

        // 释放顺序和创建顺序相反，避免通道还绑定着就销毁模块。
        if (bind_venc_ready_) {
            RK_MPI_SYS_UnBind(&vi_channel, &venc_channel);
            bind_venc_ready_ = false;
        }
        if (bind_rga_ready_) {
            RK_MPI_SYS_UnBind(&vi_channel, &rga_channel);
            bind_rga_ready_ = false;
        }
        if (venc_ready_) {
            RK_MPI_VENC_DestroyChn(VENC_CHN_ID);
            venc_ready_ = false;
        }
        if (rga_ready_) {
            RK_MPI_RGA_DestroyChn(RGA_CHN_ID);
            rga_ready_ = false;
        }
        if (vi_ready_) {
            RK_MPI_VI_DisableChn(CAMERA_ID, VI_CHN_ID);
            vi_ready_ = false;
        }
        rkmedia_ready_ = false;
    }

    static void *VencThreadEntry(void *arg) {
        static_cast<MediaPipelineImpl *>(arg)->VencLoop();
        return NULL;
    }

    static void *PushThreadEntry(void *arg) {
        static_cast<MediaPipelineImpl *>(arg)->PushLoop();
        return NULL;
    }

    static void *AiFrameThreadEntry(void *arg) {
        static_cast<MediaPipelineImpl *>(arg)->AiFrameLoop();
        return NULL;
    }

    void VencLoop() {
        while (running_.load()) {
            // 从 VENC 通道取已经编码好的 H264 数据包。
            MEDIA_BUFFER mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_VENC, VENC_CHN_ID, 100);
            if (!mb) {
                continue;
            }

            void *ptr = RK_MPI_MB_GetPtr(mb);
            size_t size = RK_MPI_MB_GetSize(mb);
            int flag = RK_MPI_MB_GetFlag(mb);
            packet_queue_.Push(ptr, (int)size, flag != 0);
            RK_MPI_MB_ReleaseBuffer(mb);
        }
    }

    void PushLoop() {
        while (running_.load()) {
            // 将 H264 包封装成 FLV/TS 并写到网络地址。
            EncodedPacket *packet = packet_queue_.Pop();
            if (!packet) {
                break;
            }

            int ret = writer_.Write(*packet);
            if (ret < 0) {
                printf("FFmpeg write packet failed: %d\n", ret);
            }
            free(packet->data);
            free(packet);
        }
    }

    void AiFrameLoop() {
        while (running_.load()) {
            // RGA 输出 RGB888 图像，按 ai_interval 抽帧送给 AI 队列。
            MEDIA_BUFFER mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_RGA, RGA_CHN_ID, 100);
            if (!mb) {
                continue;
            }

            int current_id = frame_id_++;
            ai_schedule_accumulator_ += config_.ai_fps;
            if (ai_schedule_accumulator_ >= config_.fps) {
                ai_schedule_accumulator_ -= config_.fps;
                helmet_frame_t frame;
                memset(&frame, 0, sizeof(frame));
                frame.frame_id = current_id;
                frame.timestamp_ms = (uint64_t)(av_gettime_relative() / 1000);
                frame.width = rga_width_;
                frame.height = rga_height_;
                frame.format = HELMET_IMAGE_RGB888;
                frame.data = (unsigned char *)RK_MPI_MB_GetPtr(mb);
                frame.size = (int)RK_MPI_MB_GetSize(mb);
                // PushLatest 会拷贝数据，所以这里释放 MEDIA_BUFFER 是安全的。
                ai_queue_->PushLatest(frame);
            }

            RK_MPI_MB_ReleaseBuffer(mb);
        }
    }

    media_pipeline_config_t config_;
    AiFrameQueue *ai_queue_;
    AiResultManager *result_manager_;
    std::atomic<bool> running_;
    bool rkmedia_ready_;
    bool vi_ready_;
    bool venc_ready_;
    bool rga_ready_;
    bool bind_venc_ready_;
    bool bind_rga_ready_;
    pthread_t venc_thread_;
    pthread_t push_thread_;
    pthread_t ai_thread_;
    int frame_id_;
    int ai_schedule_accumulator_;
    int rga_width_;
    int rga_height_;
    EncodedPacketQueue packet_queue_;
    FfmpegWriter writer_;
};
#endif

MediaPipeline::MediaPipeline()
    : ai_queue_(NULL), result_manager_(NULL), running_(false), impl_(NULL) {
    memset(&config_, 0, sizeof(config_));
}

MediaPipeline::~MediaPipeline() {
    Stop();
#ifdef ENABLE_RKMEDIA
    delete static_cast<MediaPipelineImpl *>(impl_);
#endif
    impl_ = NULL;
}

int MediaPipeline::Init(const media_pipeline_config_t &config,
                        AiFrameQueue *ai_queue,
                        AiResultManager *result_manager) {
    config_ = config;
    ai_queue_ = ai_queue;
    result_manager_ = result_manager;

    if (!ai_queue_ || !result_manager_) {
        return -1;
    }

#ifdef ENABLE_RKMEDIA
    if (!impl_) {
        impl_ = new MediaPipelineImpl();
    }
    return static_cast<MediaPipelineImpl *>(impl_)->Init(config_, ai_queue_, result_manager_);
#else
    // 非板端 SDK 环境下的 stub：方便先编译/阅读业务逻辑，不链接 RKMedia/FFmpeg。
    printf("MediaPipeline stub init: %dx%d fps=%d url=%s ai=%dx%d ai_fps=%d\n",
           config_.width,
           config_.height,
           config_.fps,
           config_.stream_url ? config_.stream_url : "",
           config_.ai_width,
           config_.ai_height,
           config_.ai_fps);
    return 0;
#endif
}

int MediaPipeline::Start() {
#ifdef ENABLE_RKMEDIA
    if (!impl_) {
        return -1;
    }
    const int ret = static_cast<MediaPipelineImpl *>(impl_)->Start();
    running_ = ret == 0;
    return ret;
#else
    running_ = true;
    return 0;
#endif
}

void MediaPipeline::Stop() {
#ifdef ENABLE_RKMEDIA
    if (impl_) {
        static_cast<MediaPipelineImpl *>(impl_)->Stop();
    }
#endif
    running_ = false;
}
