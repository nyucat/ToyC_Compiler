#pragma once

#include "ir/ir.h"

namespace toyc::optimizer {

class OptimizationPass {
public:
    virtual ~OptimizationPass() = default;
    virtual void run(toyc::ir::IRModule& module) = 0;
    [[nodiscard]] virtual const char* name() const = 0;
};

void runOptimizationPipeline(toyc::ir::IRModule& module, bool enableOpt);

} // namespace toyc::optimizer
