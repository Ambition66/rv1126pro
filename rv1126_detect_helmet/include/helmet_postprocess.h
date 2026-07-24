#ifndef RV1126_DETECT_HELMET_POSTPROCESS_H
#define RV1126_DETECT_HELMET_POSTPROCESS_H

#include <vector>

#include "helmet_types.h"
#include "nn_types.h"

// Decode either:
// 1. the six raw YOLO26 feature maps exported by third_party/yolo26
//    (box0, cls0, box1, cls1, box2, cls2); or
// 2. a standard end-to-end [1, N, 6] tensor containing xyxy, score, class.
int helmet_postprocess_yolo26(const std::vector<nn_tensor_t> &outputs,
                              int input_width,
                              int input_height,
                              const helmet_frame_t &frame,
                              float confidence_threshold,
                              float nms_threshold,
                              helmet_result_t *result);

#endif
