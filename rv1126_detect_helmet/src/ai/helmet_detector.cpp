#include "helmet_detector.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HelmetDetector::HelmetDetector()
    : ready_(false),
      input_width_(640),
      input_height_(640),
      want_float_(true) {
    memset(&input_tensor_, 0, sizeof(input_tensor_));
}

HelmetDetector::~HelmetDetector() {
    Release();
}

int HelmetDetector::LoadModel(const char *model_path) {
    if (!model_path) {
        return -1;
    }

    Release();

    // RknnEngine 负责底层 rknn_init 和 tensor 属性查询。
    // HelmetDetector 只关心模型输入输出如何映射到头盔检测业务。
    int ret = engine_.LoadModel(model_path);
    if (ret != NN_OK) {
        printf("HelmetDetector load RKNN failed: %s ret=%d\n", model_path, ret);
        return ret;
    }

    const std::vector<nn_tensor_attr_t> &inputs = engine_.InputAttrs();
    if (inputs.size() != 1) {
        printf("HelmetDetector expects one model input, got %u\n", (unsigned)inputs.size());
        return -2;
    }

    input_tensor_.attr = inputs[0];
    input_tensor_.attr.index = 0;
    input_tensor_.attr.type = NN_TYPE_UINT8;
    input_tensor_.attr.layout = NN_LAYOUT_NHWC;

    // RKNN 模型可能是 NCHW 或 NHWC，这里统一整理成 NHWC/RGB888 输入。
    if (inputs[0].layout == NN_LAYOUT_NCHW && inputs[0].n_dims == 4) {
        input_height_ = (int)inputs[0].dims[2];
        input_width_ = (int)inputs[0].dims[3];
    } else if (inputs[0].n_dims == 4) {
        input_height_ = (int)inputs[0].dims[1];
        input_width_ = (int)inputs[0].dims[2];
    }

    input_tensor_.attr.n_dims = 4;
    input_tensor_.attr.dims[0] = 1;
    input_tensor_.attr.dims[1] = (uint32_t)input_height_;
    input_tensor_.attr.dims[2] = (uint32_t)input_width_;
    input_tensor_.attr.dims[3] = 3;
    input_tensor_.attr.n_elems = (uint32_t)(input_width_ * input_height_ * 3);
    input_tensor_.attr.size = input_tensor_.attr.n_elems * (uint32_t)nn_type_size(input_tensor_.attr.type);
    input_tensor_.data = malloc(input_tensor_.attr.size);
    if (!input_tensor_.data) {
        return -3;
    }

    const std::vector<nn_tensor_attr_t> &outputs = engine_.OutputAttrs();
    for (size_t i = 0; i < outputs.size(); ++i) {
        nn_tensor_t tensor;
        memset(&tensor, 0, sizeof(tensor));
        tensor.attr = outputs[i];
        // want_float=true 时 RKNN 会把量化输出反量化为 float，后处理更简单。
        tensor.attr.type = want_float_ ? NN_TYPE_FLOAT32 : outputs[i].type;
        tensor.attr.size = tensor.attr.n_elems * (uint32_t)nn_type_size(tensor.attr.type);
        tensor.data = malloc(tensor.attr.size);
        if (!tensor.data) {
            FreeTensors();
            return -3;
        }
        output_tensors_.push_back(tensor);
    }

    printf("HelmetDetector loaded: model=%s input=%dx%d outputs=%u\n",
           model_path,
           input_width_,
           input_height_,
           (unsigned)output_tensors_.size());
    ready_ = true;
    return 0;
}

int HelmetDetector::Run(const helmet_frame_t &frame, helmet_result_t *result) {
    if (!ready_ || !result) {
        return -1;
    }
    if (!frame.data || frame.size <= 0) {
        return -2;
    }

    int ret = Preprocess(frame);
    if (ret != 0) {
        return ret;
    }

    ret = Inference();
    if (ret != 0) {
        return ret;
    }

    return Postprocess(frame, result);
}

void HelmetDetector::Release() {
    FreeTensors();
    engine_.Release();
    ready_ = false;
}

int HelmetDetector::Preprocess(const helmet_frame_t &frame) {
    if (!input_tensor_.data) {
        return -1;
    }
    if (frame.format != HELMET_IMAGE_RGB888 && frame.format != HELMET_IMAGE_BGR888) {
        printf("unsupported frame format for CPU preprocess: %d, use RGA/NV12 adapter later\n", frame.format);
        return -2;
    }

    unsigned char *dst = (unsigned char *)input_tensor_.data;
    const unsigned char *src = frame.data;
    const int src_stride = frame.width * 3;

    // 114 是 YOLO 系列 letterbox 常用填充值，避免边缘填充过黑或过白。
    memset(dst, 114, input_tensor_.attr.size);

    float scale_w = (float)input_width_ / (float)frame.width;
    float scale_h = (float)input_height_ / (float)frame.height;
    float scale = scale_w < scale_h ? scale_w : scale_h;
    int resize_w = (int)(frame.width * scale);
    int resize_h = (int)(frame.height * scale);
    int pad_x = (input_width_ - resize_w) / 2;
    int pad_y = (input_height_ - resize_h) / 2;

    // 当前先用 CPU 做最近邻 resize + letterbox。
    // 板端媒体链路已经让 RGA 输出 640x640 RGB888，因此正常情况下这里开销很小。
    for (int y = 0; y < resize_h; ++y) {
        int src_y = std::min((int)(y / scale), frame.height - 1);
        for (int x = 0; x < resize_w; ++x) {
            int src_x = std::min((int)(x / scale), frame.width - 1);
            const unsigned char *p = src + src_y * src_stride + src_x * 3;
            unsigned char *q = dst + ((y + pad_y) * input_width_ + (x + pad_x)) * 3;
            if (frame.format == HELMET_IMAGE_BGR888) {
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
            } else {
                q[0] = p[0];
                q[1] = p[1];
                q[2] = p[2];
            }
        }
    }

    return 0;
}

int HelmetDetector::Inference() {
    std::vector<nn_tensor_t> inputs;
    inputs.push_back(input_tensor_);
    return engine_.Run(inputs, &output_tensors_, want_float_);
}

int HelmetDetector::Postprocess(const helmet_frame_t &frame, helmet_result_t *result) {
    memset(result, 0, sizeof(*result));
    result->frame_id = frame.frame_id;
    result->timestamp_ms = frame.timestamp_ms;
    result->width = frame.width;
    result->height = frame.height;

    if (output_tensors_.empty() || !output_tensors_[0].data) {
        return 0;
    }

    const nn_tensor_t &out = output_tensors_[0];
    if (out.attr.type != NN_TYPE_FLOAT32) {
        printf("postprocess currently expects float output\n");
        return -1;
    }

    const float *data = (const float *)out.data;
    const uint32_t elem_count = out.attr.n_elems;
    // 当前是通用占位解析：每个候选框 6 个 float。
    // 拿到真实 helmet.rknn 输出 shape 后，需要在这里替换为精确 YOLO 解码/NMS。
    const uint32_t stride = 6;
    const uint32_t rows = elem_count / stride;
    for (uint32_t i = 0; i < rows && result->detection_count < HELMET_MAX_DETECTIONS; ++i) {
        const float *p = data + i * stride;
        float score = p[4];
        int class_id = (int)(p[5] + 0.5f);
        if (score < 0.35f) {
            continue;
        }
        if (class_id != HELMET_CLASS_HELMET && class_id != HELMET_CLASS_NO_HELMET) {
            continue;
        }

        float x0 = p[0];
        float y0 = p[1];
        float x1 = p[2];
        float y1 = p[3];
        // 兼容 xyxy 和 cxcywh 两种常见输出形式。
        if (x1 <= x0 || y1 <= y0) {
            x0 = p[0] - p[2] * 0.5f;
            y0 = p[1] - p[3] * 0.5f;
            x1 = p[0] + p[2] * 0.5f;
            y1 = p[1] + p[3] * 0.5f;
        }

        // 如果坐标是 0~1 归一化值，换算回当前 AI 输入帧尺寸。
        if (x1 <= 1.5f && y1 <= 1.5f) {
            x0 *= frame.width;
            x1 *= frame.width;
            y0 *= frame.height;
            y1 *= frame.height;
        }

        helmet_detection_t *det = &result->detections[result->detection_count++];
        det->class_id = class_id;
        det->confidence = score;
        det->x = std::max(0, (int)x0);
        det->y = std::max(0, (int)y0);
        det->w = std::min(frame.width - det->x, std::max(0, (int)(x1 - x0)));
        det->h = std::min(frame.height - det->y, std::max(0, (int)(y1 - y0)));
    }

    return 0;
}

void HelmetDetector::FreeTensors() {
    if (input_tensor_.data) {
        free(input_tensor_.data);
        input_tensor_.data = NULL;
    }
    for (size_t i = 0; i < output_tensors_.size(); ++i) {
        if (output_tensors_[i].data) {
            free(output_tensors_[i].data);
            output_tensors_[i].data = NULL;
        }
    }
    output_tensors_.clear();
}
