#ifndef RV1126_DETECT_HELMET_AI_WORKER_H
#define RV1126_DETECT_HELMET_AI_WORKER_H

#include <pthread.h>

#include "ai_frame_queue.h"
#include "ai_result_manager.h"
#include "helmet_detector.h"

// AI 推理线程：从 AiFrameQueue 取最新帧，调用 HelmetDetector，更新 AiResultManager。
class AiWorker {
public:
    AiWorker();
    ~AiWorker();

    int Start(AiFrameQueue *queue,
              AiResultManager *result_manager,
              HelmetDetector *detector);
    void Stop();

private:
    static void *ThreadEntry(void *arg);
    void RunLoop();

    AiFrameQueue *queue_;
    AiResultManager *result_manager_;
    HelmetDetector *detector_;
    pthread_t thread_;
    bool running_;
};

#endif
