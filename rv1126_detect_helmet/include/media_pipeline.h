#ifndef RV1126_DETECT_HELMET_MEDIA_PIPELINE_H
#define RV1126_DETECT_HELMET_MEDIA_PIPELINE_H

#include "ai_frame_queue.h"
#include "ai_result_manager.h"

// 媒体链路配置：
// width/height/fps 是主码流参数；
// ai_width/ai_height/ai_fps 是 RGA 抽帧送 AI 的参数。
typedef struct {
    int width;
    int height;
    int fps;
    int ai_width;
    int ai_height;
    const char *stream_url;
    int stream_protocol;
    int ai_fps;
} media_pipeline_config_t;

// 板端媒体管线封装。
// ENABLE_RKMEDIA=1 时创建 VI/VENC/RGA/FFmpeg 链路；
// ENABLE_RKMEDIA=0 时只保留 stub，方便在非 SDK 环境编译业务骨架。
class MediaPipeline {
public:
    MediaPipeline();
    ~MediaPipeline();

    int Init(const media_pipeline_config_t &config,
             AiFrameQueue *ai_queue,
             AiResultManager *result_manager);
    int Start();
    void Stop();

private:
    media_pipeline_config_t config_;
    AiFrameQueue *ai_queue_;
    AiResultManager *result_manager_;
    bool running_;
    void *impl_;
};

#endif
