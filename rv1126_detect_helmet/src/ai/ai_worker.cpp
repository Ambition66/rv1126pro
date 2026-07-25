#include "ai_worker.h"

#include <stdio.h>
#include <stdlib.h>

AiWorker::AiWorker()
    : queue_(NULL),
      result_manager_(NULL),
      detector_(NULL),
      thread_(0),
      running_(false) {}

AiWorker::~AiWorker() {
    Stop();
}

int AiWorker::Start(AiFrameQueue *queue,
                    AiResultManager *result_manager,
                    HelmetDetector *detector) {
    if (!queue || !result_manager || !detector) {
        return -1;
    }
    if (running_) {
        return 0;
    }

    queue_ = queue;
    result_manager_ = result_manager;
    detector_ = detector;
    running_ = true;

    int ret = pthread_create(&thread_, NULL, &AiWorker::ThreadEntry, this);
    if (ret != 0) {
        running_ = false;
        return ret;
    }
    return 0;
}

void AiWorker::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (queue_) {
        // 唤醒可能正在 PopLatest() 阻塞等待的推理线程。
        queue_->Close();
    }
    pthread_join(thread_, NULL);
    thread_ = 0;
}

void *AiWorker::ThreadEntry(void *arg) {
    AiWorker *worker = static_cast<AiWorker *>(arg);
    worker->RunLoop();
    return NULL;
}

void AiWorker::RunLoop() {
    while (running_.load()) {
        helmet_frame_t frame;
        int ret = queue_->PopLatest(&frame);
        if (ret != 0) {
            break;
        }

        helmet_result_t result;
        // 推理线程只处理最新帧，输出结果写入 result_manager 供其他业务读取。
        ret = detector_->Run(frame, &result);
        if (ret == 0) {
            result_manager_->Update(result);
        } else {
            printf("HelmetDetector run failed: frame=%d ret=%d\n", frame.frame_id, ret);
        }

        if (frame.data) {
            if (queue_->ReleaseFrame(&frame) != 0) {
                free(frame.data);
            }
        }
    }
}
