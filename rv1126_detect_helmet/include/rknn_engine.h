#ifndef RV1126_DETECT_HELMET_RKNN_ENGINE_H
#define RV1126_DETECT_HELMET_RKNN_ENGINE_H

#include "nn_engine.h"

#ifdef ENABLE_RKNN
#include <rknn_api.h>
#endif

class RknnEngine : public NnEngine {
public:
    RknnEngine();
    ~RknnEngine();

    int LoadModel(const char *model_path);
    const std::vector<nn_tensor_attr_t> &InputAttrs() const;
    const std::vector<nn_tensor_attr_t> &OutputAttrs() const;
    int Run(const std::vector<nn_tensor_t> &inputs,
            std::vector<nn_tensor_t> *outputs,
            bool want_float);
    void Release();

private:
#ifdef ENABLE_RKNN
    rknn_context ctx_;
#endif
    bool loaded_;
    uint32_t input_count_;
    uint32_t output_count_;
    std::vector<nn_tensor_attr_t> input_attrs_;
    std::vector<nn_tensor_attr_t> output_attrs_;
};

#endif
