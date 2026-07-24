#include "helmet_detector.h"
#include "helmet_postprocess.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HelmetDetector::HelmetDetector()
    : ready_(false),
      input_width_(640),
      input_height_(640),
      confidence_threshold_(0.35f),
      nms_threshold_(0.45f),
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

void HelmetDetector::SetThresholds(float confidence_threshold, float nms_threshold) {
    if (confidence_threshold > 0.0f && confidence_threshold < 1.0f) {
        confidence_threshold_ = confidence_threshold;
    }
    if (nms_threshold > 0.0f && nms_threshold < 1.0f) {
        nms_threshold_ = nms_threshold;
    }
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
    const int ret = helmet_postprocess_yolo26(output_tensors_,
                                               input_width_,
                                               input_height_,
                                               frame,
                                               confidence_threshold_,
                                               nms_threshold_,
                                               result);
    if (ret != NN_OK) {
        printf("unsupported YOLO26 output layout: tensors=%u ret=%d\n",
               (unsigned)output_tensors_.size(), ret);
    }
    return ret;
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
