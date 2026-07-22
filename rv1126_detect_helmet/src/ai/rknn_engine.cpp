#include "rknn_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, int *size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(fp);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)file_size);
    if (!data) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(data, 1, (size_t)file_size, fp);
    fclose(fp);
    if (read_size != (size_t)file_size) {
        free(data);
        return NULL;
    }

    *size = (int)file_size;
    return data;
}

#ifdef ENABLE_RKNN
// Convert RKNN tensor type to the project internal type.
// Keep this compatible with the old RV1126 rknn_api.h, which has no INT32 tensor enum.
static nn_tensor_type_t convert_type(rknn_tensor_type type) {
    switch (type) {
    case RKNN_TENSOR_INT8:
        return NN_TYPE_INT8;
    case RKNN_TENSOR_UINT8:
        return NN_TYPE_UINT8;
    case RKNN_TENSOR_FLOAT16:
        return NN_TYPE_FLOAT16;
    case RKNN_TENSOR_FLOAT32:
        return NN_TYPE_FLOAT32;
    default:
        return NN_TYPE_UNKNOWN;
    }
}

static nn_tensor_layout_t convert_layout(rknn_tensor_format fmt) {
    switch (fmt) {
    case RKNN_TENSOR_NCHW:
        return NN_LAYOUT_NCHW;
    case RKNN_TENSOR_NHWC:
        return NN_LAYOUT_NHWC;
    default:
        return NN_LAYOUT_UNKNOWN;
    }
}

static nn_tensor_attr_t convert_attr(const rknn_tensor_attr &src) {
    nn_tensor_attr_t dst;
    memset(&dst, 0, sizeof(dst));
    dst.index = src.index;
    dst.n_dims = src.n_dims > NN_MAX_DIMS ? NN_MAX_DIMS : src.n_dims;
    for (uint32_t i = 0; i < dst.n_dims; ++i) {
        dst.dims[i] = src.dims[i];
    }
    dst.n_elems = src.n_elems;
    dst.size = src.size;
    dst.type = convert_type(src.type);
    dst.layout = convert_layout(src.fmt);
    dst.zero_point = (int32_t)src.zp;
    dst.scale = src.scale;
    return dst;
}

static rknn_tensor_type to_rknn_type(nn_tensor_type_t type) {
    switch (type) {
    case NN_TYPE_INT8:
        return RKNN_TENSOR_INT8;
    case NN_TYPE_UINT8:
        return RKNN_TENSOR_UINT8;
    case NN_TYPE_FLOAT16:
        return RKNN_TENSOR_FLOAT16;
    case NN_TYPE_FLOAT32:
        return RKNN_TENSOR_FLOAT32;
    default:
        return RKNN_TENSOR_UINT8;
    }
}

static rknn_tensor_format to_rknn_layout(nn_tensor_layout_t layout) {
    switch (layout) {
    case NN_LAYOUT_NCHW:
        return RKNN_TENSOR_NCHW;
    case NN_LAYOUT_NHWC:
        return RKNN_TENSOR_NHWC;
    default:
        return RKNN_TENSOR_NHWC;
    }
}
#endif

RknnEngine::RknnEngine()
#ifdef ENABLE_RKNN
    : ctx_(0),
      loaded_(false),
#else
    : loaded_(false),
#endif
      input_count_(0),
      output_count_(0) {}

RknnEngine::~RknnEngine() {
    Release();
}

int RknnEngine::LoadModel(const char *model_path) {
#ifndef ENABLE_RKNN
    (void)model_path;
    printf("RknnEngine is built without ENABLE_RKNN\n");
    return NN_ERR_RKNN;
#else
    if (!model_path) {
        return NN_ERR_PARAM;
    }

    Release();

    // rknn_init needs the RKNN binary content, not only a file path.
    int model_size = 0;
    unsigned char *model_data = read_file(model_path, &model_size);
    if (!model_data) {
        printf("read RKNN model failed: %s\n", model_path);
        return NN_ERR_MODEL;
    }

    int ret = rknn_init(&ctx_, model_data, model_size, 0, NULL);
    free(model_data);
    if (ret != RKNN_SUCC) {
        printf("rknn_init failed: %d\n", ret);
        return NN_ERR_RKNN;
    }
    loaded_ = true;

    rknn_sdk_version version;
    memset(&version, 0, sizeof(version));
    ret = rknn_query(ctx_, RKNN_QUERY_SDK_VERSION, &version, sizeof(version));
    if (ret == RKNN_SUCC) {
        printf("RKNN api=%s driver=%s\n", version.api_version, version.drv_version);
    }

    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof(io_num));
    ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        printf("RKNN query io num failed: %d\n", ret);
        return NN_ERR_RKNN;
    }

    input_count_ = io_num.n_input;
    output_count_ = io_num.n_output;
    input_attrs_.clear();
    output_attrs_.clear();

    // Print tensor attrs at startup. These shapes drive postprocess adaptation.
    for (uint32_t i = 0; i < input_count_; ++i) {
        rknn_tensor_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
        if (ret != RKNN_SUCC) {
            printf("RKNN query input attr failed: %d\n", ret);
            return NN_ERR_RKNN;
        }
        input_attrs_.push_back(convert_attr(attr));
        printf("input[%u]: dims=%u size=%u type=%d layout=%d\n",
               i, attr.n_dims, attr.size, attr.type, attr.fmt);
    }

    for (uint32_t i = 0; i < output_count_; ++i) {
        rknn_tensor_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
        if (ret != RKNN_SUCC) {
            printf("RKNN query output attr failed: %d\n", ret);
            return NN_ERR_RKNN;
        }
        output_attrs_.push_back(convert_attr(attr));
        printf("output[%u]: dims=%u elems=%u size=%u type=%d layout=%d scale=%f zp=%u\n",
               i, attr.n_dims, attr.n_elems, attr.size, attr.type, attr.fmt, attr.scale, attr.zp);
    }

    return NN_OK;
#endif
}

const std::vector<nn_tensor_attr_t> &RknnEngine::InputAttrs() const {
    return input_attrs_;
}

const std::vector<nn_tensor_attr_t> &RknnEngine::OutputAttrs() const {
    return output_attrs_;
}

int RknnEngine::Run(const std::vector<nn_tensor_t> &inputs,
                    std::vector<nn_tensor_t> *outputs,
                    bool want_float) {
#ifndef ENABLE_RKNN
    (void)inputs;
    (void)outputs;
    (void)want_float;
    return NN_ERR_RKNN;
#else
    if (!loaded_ || !outputs) {
        return NN_ERR_PARAM;
    }
    if (inputs.size() != input_count_ || outputs->size() != output_count_) {
        return NN_ERR_IO;
    }

    std::vector<rknn_input> rknn_inputs(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        memset(&rknn_inputs[i], 0, sizeof(rknn_inputs[i]));
        rknn_inputs[i].index = inputs[i].attr.index;
        rknn_inputs[i].buf = inputs[i].data;
        rknn_inputs[i].size = inputs[i].attr.size;
        rknn_inputs[i].type = to_rknn_type(inputs[i].attr.type);
        rknn_inputs[i].fmt = to_rknn_layout(inputs[i].attr.layout);
        rknn_inputs[i].pass_through = 0;
    }

    int ret = rknn_inputs_set(ctx_, input_count_, rknn_inputs.data());
    if (ret != RKNN_SUCC) {
        printf("rknn_inputs_set failed: %d\n", ret);
        return NN_ERR_RKNN;
    }

    ret = rknn_run(ctx_, NULL);
    if (ret != RKNN_SUCC) {
        printf("rknn_run failed: %d\n", ret);
        return NN_ERR_RKNN;
    }

    std::vector<rknn_output> rknn_outputs(output_count_);
    memset(rknn_outputs.data(), 0, sizeof(rknn_output) * rknn_outputs.size());
    for (uint32_t i = 0; i < output_count_; ++i) {
        // want_float=1 asks RKNN to dequantize outputs to float for easier postprocess.
        rknn_outputs[i].want_float = want_float ? 1 : 0;
    }

    ret = rknn_outputs_get(ctx_, output_count_, rknn_outputs.data(), NULL);
    if (ret != RKNN_SUCC) {
        printf("rknn_outputs_get failed: %d\n", ret);
        return NN_ERR_RKNN;
    }

    for (uint32_t i = 0; i < output_count_; ++i) {
        nn_tensor_t &dst = (*outputs)[i];
        size_t copy_size = rknn_outputs[i].size;
        if (!dst.data || dst.attr.size < copy_size) {
            rknn_outputs_release(ctx_, output_count_, rknn_outputs.data());
            return NN_ERR_ALLOC;
        }
        memcpy(dst.data, rknn_outputs[i].buf, copy_size);
    }

    // RKNN allocates output buffers by default, so release after copying.
    rknn_outputs_release(ctx_, output_count_, rknn_outputs.data());
    return NN_OK;
#endif
}

void RknnEngine::Release() {
#ifdef ENABLE_RKNN
    if (loaded_) {
        rknn_destroy(ctx_);
    }
    ctx_ = 0;
#endif
    loaded_ = false;
    input_count_ = 0;
    output_count_ = 0;
    input_attrs_.clear();
    output_attrs_.clear();
}
