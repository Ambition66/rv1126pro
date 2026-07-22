#include "ai_frame_queue.h"

#include <stdlib.h>
#include <string.h>

AiFrameQueue::AiFrameQueue() : has_frame_(false), closed_(false) {
    pthread_mutex_init(&mutex_, NULL);
    pthread_cond_init(&cond_, NULL);
    memset(&frame_, 0, sizeof(frame_));
}

AiFrameQueue::~AiFrameQueue() {
    pthread_mutex_lock(&mutex_);
    ClearLocked();
    pthread_mutex_unlock(&mutex_);
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&cond_);
}

void AiFrameQueue::ClearLocked() {
    if (frame_.data) {
        free(frame_.data);
        frame_.data = NULL;
    }
    memset(&frame_, 0, sizeof(frame_));
    has_frame_ = false;
}

int AiFrameQueue::PushLatest(const helmet_frame_t &frame) {
    if (!frame.data || frame.size <= 0) {
        return -1;
    }

    // 这里必须拷贝帧数据：媒体层释放 MEDIA_BUFFER 后，原始指针就不能再使用。
    unsigned char *copy = (unsigned char *)malloc(frame.size);
    if (!copy) {
        return -2;
    }
    memcpy(copy, frame.data, frame.size);

    pthread_mutex_lock(&mutex_);
    if (closed_) {
        pthread_mutex_unlock(&mutex_);
        free(copy);
        return -3;
    }
    // AI 侧只需要最新画面。旧帧直接丢弃，可以避免推理慢时出现多秒延迟。
    ClearLocked();
    frame_ = frame;
    frame_.data = copy;
    has_frame_ = true;
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&mutex_);
    return 0;
}

int AiFrameQueue::PopLatest(helmet_frame_t *frame) {
    if (!frame) {
        return -1;
    }

    pthread_mutex_lock(&mutex_);
    // 没有新帧时阻塞等待；Close() 会唤醒这里，用于线程退出。
    while (!has_frame_ && !closed_) {
        pthread_cond_wait(&cond_, &mutex_);
    }
    if (closed_) {
        pthread_mutex_unlock(&mutex_);
        return 1;
    }
    *frame = frame_;
    memset(&frame_, 0, sizeof(frame_));
    has_frame_ = false;
    pthread_mutex_unlock(&mutex_);
    return 0;
}

void AiFrameQueue::Close() {
    pthread_mutex_lock(&mutex_);
    closed_ = true;
    ClearLocked();
    pthread_cond_broadcast(&cond_);
    pthread_mutex_unlock(&mutex_);
}

int AiFrameQueue::Size() {
    pthread_mutex_lock(&mutex_);
    int size = has_frame_ ? 1 : 0;
    pthread_mutex_unlock(&mutex_);
    return size;
}
