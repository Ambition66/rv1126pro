#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "ai_frame_queue.h"
#include "ai_result_manager.h"
#include "ai_worker.h"
#include "helmet_detector.h"
#include "media_pipeline.h"

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

// 主程序只负责装配各个模块：
// 1. 加载 RKNN 模型；
// 2. 启动媒体链路，把 RGA 抽帧送入 AiFrameQueue；
// 3. 启动 AI 线程，从队列取最新帧做检测；
// 4. 周期性读取最新检测结果，后续叠框/报警/上报都从这里扩展。
static void print_usage(const char *program) {
    printf("Usage:\n");
    printf("  %s --model models/helmet.rknn --stream rtmp://host/live/helmet [--protocol flv|ts] [--width 1280] [--height 720] [--fps 25] [--ai-fps 5] [--ai-width 640] [--ai-height 640] [--conf 0.35] [--nms 0.45]\n", program);
}

static const char *get_arg(int argc, char **argv, const char *name, const char *default_value) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return default_value;
}

int main(int argc, char **argv) {
    const char *model_path = get_arg(argc, argv, "--model", "models/helmet.rknn");
    const char *stream_url = get_arg(argc, argv, "--stream", NULL);
    const char *protocol = get_arg(argc, argv, "--protocol", "flv");
    int width = atoi(get_arg(argc, argv, "--width", "1280"));
    int height = atoi(get_arg(argc, argv, "--height", "720"));
    int fps = atoi(get_arg(argc, argv, "--fps", "25"));
    int ai_fps = atoi(get_arg(argc, argv, "--ai-fps", "5"));
    int ai_width = atoi(get_arg(argc, argv, "--ai-width", "640"));
    int ai_height = atoi(get_arg(argc, argv, "--ai-height", "640"));
    float confidence_threshold = (float)atof(get_arg(argc, argv, "--conf", "0.35"));
    float nms_threshold = (float)atof(get_arg(argc, argv, "--nms", "0.45"));

    if (!stream_url || width <= 0 || height <= 0 || fps <= 0 || ai_fps <= 0 ||
        ai_width <= 0 || ai_height <= 0 || ai_fps > fps ||
        confidence_threshold <= 0.0f || confidence_threshold >= 1.0f ||
        nms_threshold <= 0.0f || nms_threshold >= 1.0f ||
        (strcmp(protocol, "flv") != 0 && strcmp(protocol, "ts") != 0)) {
        print_usage(argv[0]);
        return -1;
    }

    AiFrameQueue ai_queue;
    AiResultManager result_manager;

    // 检测器先加载模型。媒体线程启动后会持续把 AI 帧送入 ai_queue，
    // AiWorker 再复用这个 detector 做推理。
    HelmetDetector detector;
    detector.SetThresholds(confidence_threshold, nms_threshold);
    if (detector.LoadModel(model_path) != 0) {
        printf("load model failed: %s\n", model_path);
        return -1;
    }

    // media_config 对应板端媒体链路参数：
    // width/height/fps 是主码流；ai_width/ai_height/ai_fps 是 AI 抽帧分支。
    media_pipeline_config_t media_config;
    memset(&media_config, 0, sizeof(media_config));
    media_config.width = width;
    media_config.height = height;
    media_config.fps = fps;
    media_config.ai_width = ai_width;
    media_config.ai_height = ai_height;
    media_config.stream_url = stream_url;
    media_config.stream_protocol = strcmp(protocol, "ts") == 0 ? 1 : 0;
    media_config.ai_fps = ai_fps;

    MediaPipeline pipeline;
    if (pipeline.Init(media_config, &ai_queue, &result_manager) != 0) {
        printf("media pipeline init failed\n");
        return -1;
    }

    if (pipeline.Start() != 0) {
        printf("media pipeline start failed\n");
        return -1;
    }

    // AI 推理独立成线程，避免 RKNN 推理阻塞 VENC/FFmpeg 推流线程。
    AiWorker ai_worker;
    if (ai_worker.Start(&ai_queue, &result_manager, &detector) != 0) {
        printf("ai worker start failed\n");
        pipeline.Stop();
        return -1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    printf("rv1126_detect_helmet started\n");
    int last_printed_frame = -1;
    while (keep_running) {
        sleep(1);
        helmet_result_t result;
        // 这里只打印最新结果。后续可以在这里接入告警、截图、平台上报等业务逻辑。
        if (result_manager.GetLatest(&result) && result.frame_id != last_printed_frame) {
            last_printed_frame = result.frame_id;
            printf("latest result: frame=%d detections=%d\n",
                   result.frame_id,
                   result.detection_count);
            for (int i = 0; i < result.detection_count; ++i) {
                const helmet_detection_t &detection = result.detections[i];
                printf("  class=%d confidence=%.3f box=[%d,%d,%d,%d]\n",
                       detection.class_id,
                       detection.confidence,
                       detection.x,
                       detection.y,
                       detection.w,
                       detection.h);
            }
        }
    }

    printf("stopping...\n");
    pipeline.Stop();
    ai_worker.Stop();
    return 0;
}
