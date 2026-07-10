#include "optimizer/optimizer.h"

#include "optimizer/cfg_simplify.h"
#include "optimizer/constant_fold.h"
#include "optimizer/copy_prop.h"
#include "optimizer/cse.h"
#include "optimizer/dead_code.h"

namespace toyc::optimizer {

void runOptimizationPipeline(toyc::ir::IRModule& module, bool enableOpt) {
    if (!enableOpt) {
        return;
    }

    ConstantFoldPass constantFold;
    CopyPropPass copyProp;
    CsePass cse;
    CfgSimplifyPass cfgSimplify;
    DeadCodeEliminationPass deadCode;

    for (int round = 0; round < 3; ++round) {
        constantFold.run(module);
        copyProp.run(module);
        cse.run(module);
        cfgSimplify.run(module);
        deadCode.run(module);
    }
}

} // namespace toyc::optimizer
