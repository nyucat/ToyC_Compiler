#pragma once

#include "optimizer/optimizer.h"

namespace toyc::optimizer {

class DeadCodeEliminationPass final : public OptimizationPass {
public:
    void run(toyc::ir::IRModule& module) override;
    [[nodiscard]] const char* name() const override { return "dead-code-elimination"; }
};

} // namespace toyc::optimizer
