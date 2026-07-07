#pragma once

#include "optimizer/optimizer.h"

namespace toyc::optimizer {

class CfgSimplifyPass final : public OptimizationPass {
public:
    void run(toyc::ir::IRModule& module) override;
    [[nodiscard]] const char* name() const override { return "cfg-simplify"; }
};

} // namespace toyc::optimizer
