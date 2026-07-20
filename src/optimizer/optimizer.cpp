#include "optimizer/optimizer.h"

#include "optimizer/constant_fold.h"
#include "optimizer/dead_code.h"

namespace toyc::optimizer {

void runOptimizationPipeline(toyc::ir::IRModule& module, bool enableOpt) {
    if (!enableOpt) {
        return;
    }

    ConstantFoldPass constantFold;
    DeadCodeEliminationPass deadCode;

    for (int round = 0; round < 2; ++round) {
        constantFold.run(module);
        deadCode.run(module);
    }
}

} // namespace toyc::optimizer
