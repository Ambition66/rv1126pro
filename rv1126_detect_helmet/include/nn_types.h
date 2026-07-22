#ifndef RV1126_DETECT_HELMET_NN_TYPES_H
#define RV1126_DETECT_HELMET_NN_TYPES_H

#include <stdint.h>
#include <stdlib.h>

#define NN_MAX_DIMS 8

enum nn_status_t {
    NN_OK = 0,
    NN_ERR_PARAM = -1,
    NN_ERR_MODEL = -2,
    NN_ERR_RKNN = -3,
    NN_ERR_IO = -4,
    NN_ERR_ALLOC = -5,
};

enum nn_tensor_layout_t {
    NN_LAYOUT_UNKNOWN = 0,
    NN_LAYOUT_NCHW = 1,
    NN_LAYOUT_NHWC = 2,
};

enum nn_tensor_type_t {
    NN_TYPE_UNKNOWN = 0,
    NN_TYPE_INT8 = 1,
    NN_TYPE_UINT8 = 2,
    NN_TYPE_FLOAT16 = 3,
    NN_TYPE_FLOAT32 = 4,
    NN_TYPE_INT32 = 5,
};

typedef struct {
    uint32_t index;
    uint32_t n_dims;
    uint32_t dims[NN_MAX_DIMS];
    uint32_t n_elems;
    uint32_t size;
    nn_tensor_type_t type;
    nn_tensor_layout_t layout;
    int32_t zero_point;
    float scale;
} nn_tensor_attr_t;

typedef struct {
    nn_tensor_attr_t attr;
    void *data;
} nn_tensor_t;

static inline size_t nn_type_size(nn_tensor_type_t type) {
    switch (type) {
    case NN_TYPE_INT8:
        return sizeof(int8_t);
    case NN_TYPE_UINT8:
        return sizeof(uint8_t);
    case NN_TYPE_FLOAT16:
        return sizeof(uint16_t);
    case NN_TYPE_FLOAT32:
        return sizeof(float);
    case NN_TYPE_INT32:
        return sizeof(int32_t);
    default:
        return 0;
    }
}

#endif
