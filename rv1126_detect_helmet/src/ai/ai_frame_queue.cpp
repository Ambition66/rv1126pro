#include "ai_frame_queue.h"

#include <stdlib.h>
#include <string.h>

AiFrameQueue::AiFrameQueue()
    : queued_buffer_(-1), has_frame_(false), closed_(false) {
    pthread_mutex_init(&mutex_, NULL);
    pthread_cond_init(&cond_, NULL);
    memset(&frame_, 0, sizeof(frame_));
    memset(buffers_, 0, sizeof(buffers_));
    memset(buffer_capacities_, 0, sizeof(buffer_capacities_));
    memset(buffer_in_use_, 0, sizeof(buffer_in_use_));
}

AiFrameQueue::~AiFrameQueue() {
    pthread_mutex_lock(&mutex_);
    ClearLocked();
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        free(buffers_[i]);
        buffers_[i] = NULL;
        buffer_capacities_[i] = 0;
        buffer_in_use_[i] = false;
    }
    pthread_mutex_unlock(&mutex_);
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&cond_);
}

void AiFrameQueue::ClearLocked() {
    if (queued_buffer_ >= 0) {
        buffer_in_use_[queued_buffer_] = false;
    }
    memset(&frame_, 0, sizeof(frame_));
    queued_buffer_ = -1;
    has_frame_ = false;
}

int AiFrameQueue::PushLatest(const helmet_frame_t &frame) {
    if (!frame.data || frame.size <= 0) {
        return -1;
    }

    pthread_mutex_lock(&mutex_);
    if (closed_) {
        pthread_mutex_unlock(&mutex_);
        return -3;
    }

    // Reuse the queued slot when replacing a stale frame. Otherwise select a
    // slot that is not currently owned by the AI worker.
    int slot = queued_buffer_;
    if (slot < 0) {
        for (int i = 0; i < BUFFER_COUNT; ++i) {
            if (!buffer_in_use_[i]) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mutex_);
        return -2;
    }

    // Buffers grow only when the input shape grows, not once per frame.
    if (buffer_capacities_[slot] < (size_t)frame.size) {
        unsigned char *resized =
            (unsigned char *)realloc(buffers_[slot], (size_t)frame.size);
        if (!resized) {
            pthread_mutex_unlock(&mutex_);
            return -2;
        }
        buffers_[slot] = resized;
        buffer_capacities_[slot] = (size_t)frame.size;
    }

    memcpy(buffers_[slot], frame.data, (size_t)frame.size);
    frame_ = frame;
    frame_.data = buffers_[slot];
    buffer_in_use_[slot] = true;
    queued_buffer_ = slot;
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
    while (!has_frame_ && !closed_) {
        pthread_cond_wait(&cond_, &mutex_);
    }
    if (closed_) {
        pthread_mutex_unlock(&mutex_);
        return 1;
    }

    *frame = frame_;
    memset(&frame_, 0, sizeof(frame_));
    queued_buffer_ = -1;
    has_frame_ = false;
    pthread_mutex_unlock(&mutex_);
    return 0;
}

int AiFrameQueue::ReleaseFrame(helmet_frame_t *frame) {
    if (!frame || !frame->data) {
        return -1;
    }

    pthread_mutex_lock(&mutex_);
    int released = -1;
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        if (buffers_[i] == frame->data) {
            buffer_in_use_[i] = false;
            released = 0;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_);

    if (released == 0) {
        memset(frame, 0, sizeof(*frame));
    }
    return released;
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
