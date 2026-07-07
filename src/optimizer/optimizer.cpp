#include "optimizer/optimizer.h"

#include "optimizer/cfg_simplify.h"
#include "optimizer/constant_fold.h"
#include "optimizer/dead_code.h"

#include <memory>
#include <vector>

namespace toyc::optimizer {

void runOptimizationPipeline(toyc::ir::IRModule& module, bool enableOpt) {
    if (!enableOpt) {
        return;
    }

    ConstantFoldPass constantFold;
    CfgSimplifyPass cfgSimplify;
    DeadCodeEliminationPass deadCode;

    for (int round = 0; round < 2; ++round) {
        constantFold.run(module);
        cfgSimplify.run(module);
        deadCode.run(module);
    }
}

} // namespace toyc::optimizer
