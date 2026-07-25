#include "helmet_postprocess.h"

#include <algorithm>
#include <cmath>
#include <string.h>

namespace {

struct Candidate {
    int class_id;
    float score;
    float x0;
    float y0;
    float x1;
    float y1;
};

struct FeatureShape {
    int channels;
    int height;
    int width;
    bool nhwc;
};

static float sigmoid(float value) {
    if (value >= 0.0f) {
        const float z = expf(-value);
        return 1.0f / (1.0f + z);
    }
    const float z = expf(value);
    return z / (1.0f + z);
}

static bool get_feature_shape(const nn_tensor_t &tensor, FeatureShape *shape) {
    const bool supported_type =
        tensor.attr.type == NN_TYPE_FLOAT32 ||
        tensor.attr.type == NN_TYPE_INT8 ||
        tensor.attr.type == NN_TYPE_UINT8;
    if (!shape || !tensor.data || !supported_type || tensor.attr.n_dims != 4) {
        return false;
    }
    if (tensor.attr.type != NN_TYPE_FLOAT32 && tensor.attr.scale <= 0.0f) {
        return false;
    }

    if (tensor.attr.layout == NN_LAYOUT_NHWC) {
        shape->height = (int)tensor.attr.dims[1];
        shape->width = (int)tensor.attr.dims[2];
        shape->channels = (int)tensor.attr.dims[3];
        shape->nhwc = true;
    } else {
        shape->channels = (int)tensor.attr.dims[1];
        shape->height = (int)tensor.attr.dims[2];
        shape->width = (int)tensor.attr.dims[3];
        shape->nhwc = false;
    }
    return shape->channels > 0 && shape->height > 0 && shape->width > 0;
}

static float feature_value(const nn_tensor_t &tensor,
                           const FeatureShape &shape,
                           int channel,
                           int y,
                           int x) {
    size_t index;
    if (shape.nhwc) {
        index = ((size_t)y * shape.width + x) * shape.channels + channel;
    } else {
        index = ((size_t)channel * shape.height + y) * shape.width + x;
    }
    if (tensor.attr.type == NN_TYPE_FLOAT32) {
        const float *data = static_cast<const float *>(tensor.data);
        return data[index];
    }
    if (tensor.attr.type == NN_TYPE_INT8) {
        const int8_t *data = static_cast<const int8_t *>(tensor.data);
        return ((int32_t)data[index] - tensor.attr.zero_point) * tensor.attr.scale;
    }
    const uint8_t *data = static_cast<const uint8_t *>(tensor.data);
    return ((int32_t)data[index] - tensor.attr.zero_point) * tensor.attr.scale;
}

static float decode_distance(const nn_tensor_t &box,
                             const FeatureShape &shape,
                             int side,
                             int reg_max,
                             int y,
                             int x) {
    // YOLO26 uses nn.Identity instead of DFL for reg_max == 1, so these four
    // channels are direct ltrb distances rather than one-bin distributions.
    if (reg_max == 1) {
        return std::max(0.0f, feature_value(box, shape, side, y, x));
    }

    float max_logit = feature_value(box, shape, side * reg_max, y, x);
    for (int bin = 1; bin < reg_max; ++bin) {
        max_logit = std::max(max_logit, feature_value(box, shape, side * reg_max + bin, y, x));
    }

    float sum = 0.0f;
    float weighted_sum = 0.0f;
    for (int bin = 0; bin < reg_max; ++bin) {
        const float probability = expf(feature_value(box, shape, side * reg_max + bin, y, x) - max_logit);
        sum += probability;
        weighted_sum += probability * bin;
    }
    return sum > 0.0f ? weighted_sum / sum : 0.0f;
}

static float intersection_over_union(const Candidate &a, const Candidate &b) {
    const float left = std::max(a.x0, b.x0);
    const float top = std::max(a.y0, b.y0);
    const float right = std::min(a.x1, b.x1);
    const float bottom = std::min(a.y1, b.y1);
    const float intersection = std::max(0.0f, right - left) * std::max(0.0f, bottom - top);
    const float area_a = std::max(0.0f, a.x1 - a.x0) * std::max(0.0f, a.y1 - a.y0);
    const float area_b = std::max(0.0f, b.x1 - b.x0) * std::max(0.0f, b.y1 - b.y0);
    const float union_area = area_a + area_b - intersection;
    return union_area > 0.0f ? intersection / union_area : 0.0f;
}

static bool candidate_score_desc(const Candidate &a, const Candidate &b) {
    return a.score > b.score;
}

static void decode_end_to_end(const nn_tensor_t &output,
                              float confidence_threshold,
                              std::vector<Candidate> *candidates) {
    if (!output.data || output.attr.type != NN_TYPE_FLOAT32 || output.attr.n_elems < 6) {
        return;
    }
    if (output.attr.n_dims < 2 || output.attr.dims[output.attr.n_dims - 1] != 6) {
        return;
    }

    const float *data = static_cast<const float *>(output.data);
    const uint32_t rows = output.attr.n_elems / 6;
    for (uint32_t i = 0; i < rows; ++i) {
        const float *row = data + i * 6;
        const int class_id = (int)(row[5] + 0.5f);
        if (row[4] < confidence_threshold || class_id < 0 || class_id > 1) {
            continue;
        }
        Candidate candidate = {class_id, row[4], row[0], row[1], row[2], row[3]};
        if (candidate.x1 > candidate.x0 && candidate.y1 > candidate.y0) {
            candidates->push_back(candidate);
        }
    }
}

static bool decode_feature_pair(const nn_tensor_t &box,
                                const nn_tensor_t &classification,
                                int input_width,
                                int input_height,
                                float confidence_threshold,
                                std::vector<Candidate> *candidates) {
    FeatureShape box_shape;
    FeatureShape cls_shape;
    if (!get_feature_shape(box, &box_shape) || !get_feature_shape(classification, &cls_shape)) {
        return false;
    }
    if (box_shape.height != cls_shape.height || box_shape.width != cls_shape.width ||
        box_shape.channels % 4 != 0 || cls_shape.channels < 2) {
        return false;
    }

    const int reg_max = box_shape.channels / 4;
    const float stride_x = (float)input_width / box_shape.width;
    const float stride_y = (float)input_height / box_shape.height;
    for (int y = 0; y < box_shape.height; ++y) {
        for (int x = 0; x < box_shape.width; ++x) {
            int class_id = 0;
            float best_score = sigmoid(feature_value(classification, cls_shape, 0, y, x));
            for (int c = 1; c < cls_shape.channels; ++c) {
                const float score = sigmoid(feature_value(classification, cls_shape, c, y, x));
                if (score > best_score) {
                    best_score = score;
                    class_id = c;
                }
            }
            if (best_score < confidence_threshold || class_id > 1) {
                continue;
            }

            const float anchor_x = x + 0.5f;
            const float anchor_y = y + 0.5f;
            Candidate candidate;
            candidate.class_id = class_id;
            candidate.score = best_score;
            candidate.x0 = (anchor_x - decode_distance(box, box_shape, 0, reg_max, y, x)) * stride_x;
            candidate.y0 = (anchor_y - decode_distance(box, box_shape, 1, reg_max, y, x)) * stride_y;
            candidate.x1 = (anchor_x + decode_distance(box, box_shape, 2, reg_max, y, x)) * stride_x;
            candidate.y1 = (anchor_y + decode_distance(box, box_shape, 3, reg_max, y, x)) * stride_y;
            if (candidate.x1 > candidate.x0 && candidate.y1 > candidate.y0) {
                candidates->push_back(candidate);
            }
        }
    }
    return true;
}

static void write_results(std::vector<Candidate> *candidates,
                          int input_width,
                          int input_height,
                          const helmet_frame_t &frame,
                          float nms_threshold,
                          helmet_result_t *result) {
    std::sort(candidates->begin(), candidates->end(), candidate_score_desc);

    const float scale = std::min((float)input_width / frame.width, (float)input_height / frame.height);
    const float pad_x = (input_width - frame.width * scale) * 0.5f;
    const float pad_y = (input_height - frame.height * scale) * 0.5f;
    std::vector<Candidate> selected;
    for (size_t i = 0; i < candidates->size() && selected.size() < HELMET_MAX_DETECTIONS; ++i) {
        const Candidate &candidate = (*candidates)[i];
        bool suppressed = false;
        for (size_t j = 0; j < selected.size(); ++j) {
            if (candidate.class_id == selected[j].class_id &&
                intersection_over_union(candidate, selected[j]) > nms_threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            selected.push_back(candidate);
        }
    }

    for (size_t i = 0; i < selected.size(); ++i) {
        const Candidate &candidate = selected[i];
        const float x0 = std::max(0.0f, std::min((float)frame.width, (candidate.x0 - pad_x) / scale));
        const float y0 = std::max(0.0f, std::min((float)frame.height, (candidate.y0 - pad_y) / scale));
        const float x1 = std::max(0.0f, std::min((float)frame.width, (candidate.x1 - pad_x) / scale));
        const float y1 = std::max(0.0f, std::min((float)frame.height, (candidate.y1 - pad_y) / scale));
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }

        helmet_detection_t *detection = &result->detections[result->detection_count++];
        detection->class_id = candidate.class_id;
        detection->confidence = candidate.score;
        detection->x = (int)x0;
        detection->y = (int)y0;
        detection->w = (int)(x1 - x0);
        detection->h = (int)(y1 - y0);
    }
}

}  // namespace

int helmet_postprocess_yolo26(const std::vector<nn_tensor_t> &outputs,
                              int input_width,
                              int input_height,
                              const helmet_frame_t &frame,
                              float confidence_threshold,
                              float nms_threshold,
                              helmet_result_t *result) {
    if (!result || input_width <= 0 || input_height <= 0 || frame.width <= 0 || frame.height <= 0) {
        return NN_ERR_PARAM;
    }

    memset(result, 0, sizeof(*result));
    result->frame_id = frame.frame_id;
    result->timestamp_ms = frame.timestamp_ms;
    result->width = frame.width;
    result->height = frame.height;

    std::vector<Candidate> candidates;
    bool decoded = false;
    if (outputs.size() == 1) {
        decode_end_to_end(outputs[0], confidence_threshold, &candidates);
        decoded = outputs[0].attr.n_dims >= 2 &&
                  outputs[0].attr.dims[outputs[0].attr.n_dims - 1] == 6;
    } else if (outputs.size() >= 2 && outputs.size() % 2 == 0) {
        decoded = true;
        for (size_t i = 0; i < outputs.size(); i += 2) {
            if (!decode_feature_pair(outputs[i], outputs[i + 1], input_width, input_height,
                                     confidence_threshold, &candidates)) {
                decoded = false;
                break;
            }
        }
    }

    if (!decoded) {
        return NN_ERR_IO;
    }
    write_results(&candidates, input_width, input_height, frame, nms_threshold, result);
    return NN_OK;
}
