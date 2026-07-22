#include "ai_result_manager.h"

#include <string.h>

AiResultManager::AiResultManager() : has_result_(false) {
    pthread_mutex_init(&mutex_, NULL);
    memset(&result_, 0, sizeof(result_));
}

AiResultManager::~AiResultManager() {
    pthread_mutex_destroy(&mutex_);
}

void AiResultManager::Update(const helmet_result_t &result) {
    pthread_mutex_lock(&mutex_);
    // 覆盖旧结果即可，业务侧通常只关心当前画面的最新检测状态。
    result_ = result;
    has_result_ = true;
    pthread_mutex_unlock(&mutex_);
}

bool AiResultManager::GetLatest(helmet_result_t *result) {
    if (!result) {
        return false;
    }
    pthread_mutex_lock(&mutex_);
    bool ok = has_result_;
    if (ok) {
        // 返回结构体拷贝，避免调用者持有内部锁或内部指针。
        *result = result_;
    }
    pthread_mutex_unlock(&mutex_);
    return ok;
}
