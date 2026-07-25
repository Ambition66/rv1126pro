#include <assert.h>
#include <math.h>
#include <string.h>

#include <vector>

#include "helmet_postprocess.h"

static nn_tensor_t make_nchw_tensor(float *data, int channels, int height, int width) {
    nn_tensor_t tensor;
    memset(&tensor, 0, sizeof(tensor));
    tensor.data = data;
    tensor.attr.type = NN_TYPE_FLOAT32;
    tensor.attr.layout = NN_LAYOUT_NCHW;
    tensor.attr.n_dims = 4;
    tensor.attr.dims[0] = 1;
    tensor.attr.dims[1] = channels;
    tensor.attr.dims[2] = height;
    tensor.attr.dims[3] = width;
    tensor.attr.n_elems = channels * height * width;
    tensor.attr.size = tensor.attr.n_elems * sizeof(float);
    return tensor;
}

static nn_tensor_t make_int8_nchw_tensor(int8_t *data,
                                         int channels,
                                         int height,
                                         int width,
                                         float scale,
                                         int zero_point) {
    nn_tensor_t tensor;
    memset(&tensor, 0, sizeof(tensor));
    tensor.data = data;
    tensor.attr.type = NN_TYPE_INT8;
    tensor.attr.layout = NN_LAYOUT_NCHW;
    tensor.attr.n_dims = 4;
    tensor.attr.dims[0] = 1;
    tensor.attr.dims[1] = channels;
    tensor.attr.dims[2] = height;
    tensor.attr.dims[3] = width;
    tensor.attr.n_elems = channels * height * width;
    tensor.attr.size = tensor.attr.n_elems * sizeof(int8_t);
    tensor.attr.scale = scale;
    tensor.attr.zero_point = zero_point;
    return tensor;
}

static helmet_frame_t make_frame(int width, int height) {
    helmet_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.frame_id = 7;
    frame.timestamp_ms = 1234;
    frame.width = width;
    frame.height = height;
    return frame;
}

static void test_yolo26_six_output_pair_format() {
    // One feature cell, reg_max=1. Distances of 0.25 around the cell center
    // decode to a 320x320 box in a 640x640 model input.
    float box_data[] = {0.25f, 0.25f, 0.25f, 0.25f};
    float class_data[] = {10.0f, -10.0f};
    std::vector<nn_tensor_t> outputs;
    outputs.push_back(make_nchw_tensor(box_data, 4, 1, 1));
    outputs.push_back(make_nchw_tensor(class_data, 2, 1, 1));

    helmet_result_t result;
    const helmet_frame_t frame = make_frame(640, 640);
    assert(helmet_postprocess_yolo26(outputs, 640, 640, frame, 0.35f, 0.45f, &result) == NN_OK);
    assert(result.frame_id == 7);
    assert(result.detection_count == 1);
    assert(result.detections[0].class_id == HELMET_CLASS_HELMET);
    assert(result.detections[0].confidence > 0.99f);
    assert(result.detections[0].x == 160);
    assert(result.detections[0].y == 160);
    assert(result.detections[0].w == 320);
    assert(result.detections[0].h == 320);
}

static void test_end_to_end_nms_and_letterbox_mapping() {
    float detections[] = {
        160.0f, 160.0f, 480.0f, 480.0f, 0.90f, 1.0f,
        170.0f, 170.0f, 470.0f, 470.0f, 0.80f, 1.0f,
    };
    nn_tensor_t output;
    memset(&output, 0, sizeof(output));
    output.data = detections;
    output.attr.type = NN_TYPE_FLOAT32;
    output.attr.layout = NN_LAYOUT_NHWC;
    output.attr.n_dims = 3;
    output.attr.dims[0] = 1;
    output.attr.dims[1] = 2;
    output.attr.dims[2] = 6;
    output.attr.n_elems = 12;
    output.attr.size = sizeof(detections);

    std::vector<nn_tensor_t> outputs(1, output);
    helmet_result_t result;
    // A 640x360 source is letterboxed with 140 pixels of vertical padding.
    const helmet_frame_t frame = make_frame(640, 360);
    assert(helmet_postprocess_yolo26(outputs, 640, 640, frame, 0.35f, 0.45f, &result) == NN_OK);
    assert(result.detection_count == 1);
    assert(result.detections[0].class_id == HELMET_CLASS_NO_HELMET);
    assert(result.detections[0].x == 160);
    assert(result.detections[0].y == 20);
    assert(result.detections[0].w == 320);
    assert(result.detections[0].h == 320);
}

static void test_quantized_yolo26_pair_format() {
    // scale=0.01 produces direct ltrb distances of 0.25.
    int8_t box_data[] = {25, 25, 25, 25};
    // scale=0.1 produces class logits 10 and -10.
    int8_t class_data[] = {100, -100};
    std::vector<nn_tensor_t> outputs;
    outputs.push_back(make_int8_nchw_tensor(box_data, 4, 1, 1, 0.01f, 0));
    outputs.push_back(make_int8_nchw_tensor(class_data, 2, 1, 1, 0.1f, 0));

    helmet_result_t result;
    const helmet_frame_t frame = make_frame(640, 640);
    assert(helmet_postprocess_yolo26(outputs, 640, 640, frame, 0.35f, 0.45f, &result) == NN_OK);
    assert(result.detection_count == 1);
    assert(result.detections[0].class_id == HELMET_CLASS_HELMET);
    assert(result.detections[0].confidence > 0.99f);
    assert(result.detections[0].x == 160);
    assert(result.detections[0].y == 160);
    assert(result.detections[0].w == 320);
    assert(result.detections[0].h == 320);
}

int main() {
    test_yolo26_six_output_pair_format();
    test_end_to_end_nms_and_letterbox_mapping();
    test_quantized_yolo26_pair_format();
    return 0;
}
