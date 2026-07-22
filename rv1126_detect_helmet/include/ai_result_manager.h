#ifndef RV1126_DETECT_HELMET_AI_RESULT_MANAGER_H
#define RV1126_DETECT_HELMET_AI_RESULT_MANAGER_H

#include <pthread.h>

#include "helmet_types.h"

// 最新检测结果缓存。
// 当前只保存最近一帧结果，适合叠框、报警、抓拍、上报等业务读取。
class AiResultManager {
public:
    AiResultManager();
    ~AiResultManager();

    void Update(const helmet_result_t &result);
    bool GetLatest(helmet_result_t *result);

private:
    pthread_mutex_t mutex_;
    helmet_result_t result_;
    bool has_result_;
};

#endif
