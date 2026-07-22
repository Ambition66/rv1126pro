#ifndef RV1126_DETECT_HELMET_NN_ENGINE_H
#define RV1126_DETECT_HELMET_NN_ENGINE_H

#include <vector>

#include "nn_types.h"

class NnEngine {
public:
    virtual ~NnEngine() {}

    virtual int LoadModel(const char *model_path) = 0;
    virtual const std::vector<nn_tensor_attr_t> &InputAttrs() const = 0;
    virtual const std::vector<nn_tensor_attr_t> &OutputAttrs() const = 0;
    virtual int Run(const std::vector<nn_tensor_t> &inputs,
                    std::vector<nn_tensor_t> *outputs,
                    bool want_float) = 0;
};

#endif
