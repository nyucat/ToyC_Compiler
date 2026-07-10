#include "optimizer/optimizer.h"

#include "optimizer/cfg_simplify.h"
#include "optimizer/constant_fold.h"
#include "optimizer/copy_prop.h"
#include "optimizer/cse.h"
#include "optimizer/dead_code.h"

#include <memory>
#include <vector>

namespace toyc::optimizer {

void runOptimizationPipeline(toyc::ir::IRModule& module, bool enableOpt) {
    if (!enableOpt) {
        return;
    }

    CopyPropPass copyProp;
    CsePass cse;
    ConstantFoldPass constantFold;
    DeadCodeEliminationPass deadCode;

    for (int round = 0; round < 4; ++round) {
        copyProp.run(module);
        cse.run(module);
        constantFold.run(module);
        deadCode.run(module);
    }
}

} // namespace toyc::optimizer
