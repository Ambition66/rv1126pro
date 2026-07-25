#ifndef RV1126_DETECT_HELMET_DETECTOR_H
#define RV1126_DETECT_HELMET_DETECTOR_H

#include "helmet_types.h"
#include "rknn_engine.h"

#include <stdint.h>
#include <vector>

// 头盔检测业务封装：把一帧 RGB/BGR 图像转换为 helmet_result_t。
// 底层推理由 RknnEngine 完成，这里负责前处理、推理调度和后处理。
class HelmetDetector {
public:
    HelmetDetector();
    ~HelmetDetector();

    int LoadModel(const char *model_path);
    void SetThresholds(float confidence_threshold, float nms_threshold);
    int Run(const helmet_frame_t &frame, helmet_result_t *result);
    void Release();

private:
    int Preprocess(const helmet_frame_t &frame);
    int Inference();
    int Postprocess(const helmet_frame_t &frame, helmet_result_t *result);
    void FreeTensors();

    bool ready_;
    int input_width_;
    int input_height_;
    float confidence_threshold_;
    float nms_threshold_;
    RknnEngine engine_;
    nn_tensor_t input_tensor_;
    std::vector<nn_tensor_t> output_tensors_;
    bool want_float_;
    uint64_t perf_count_;
    uint64_t preprocess_total_us_;
    uint64_t inference_total_us_;
    uint64_t postprocess_total_us_;
};

#endif
