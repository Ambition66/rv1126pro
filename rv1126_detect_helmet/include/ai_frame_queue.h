#ifndef RV1126_DETECT_HELMET_AI_FRAME_QUEUE_H
#define RV1126_DETECT_HELMET_AI_FRAME_QUEUE_H

#include <stddef.h>
#include <pthread.h>

#include "helmet_types.h"

// AI 最新帧队列。
// 它不是 FIFO：新帧会替换旧帧，避免 AI 推理慢时堆积历史帧。
class AiFrameQueue {
public:
    AiFrameQueue();
    ~AiFrameQueue();

    int PushLatest(const helmet_frame_t &frame);
    int PopLatest(helmet_frame_t *frame);
    int ReleaseFrame(helmet_frame_t *frame);
    void Close();
    int Size();

private:
    enum { BUFFER_COUNT = 3 };
    void ClearLocked();

    pthread_mutex_t mutex_;
    pthread_cond_t cond_;
    helmet_frame_t frame_;
    unsigned char *buffers_[BUFFER_COUNT];
    size_t buffer_capacities_[BUFFER_COUNT];
    bool buffer_in_use_[BUFFER_COUNT];
    int queued_buffer_;
    bool has_frame_;
    bool closed_;
};

#endif
