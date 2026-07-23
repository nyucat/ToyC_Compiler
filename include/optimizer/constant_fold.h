#pragma once

#include "optimizer/optimizer.h"

namespace toyc::optimizer {

class ConstantFoldPass final : public OptimizationPass {
public:
    void run(toyc::ir::IRModule& module) override;
    [[nodiscard]] const char* name() const override { return "constant-fold"; }
};

} // namespace toyc::optimizer
