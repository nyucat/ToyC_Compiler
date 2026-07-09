#include "optimizer/dead_code.h"

#include "ir/basic_block.h"

namespace toyc::optimizer {

void DeadCodeEliminationPass::run(toyc::ir::IRModule& module) {
    using namespace toyc::ir;

    for (auto& function : module.functions) {
        for (auto& block : function.blocks) {
            std::vector<IRInstruction> live;
            live.reserve(block.instructions().size());

            for (auto& inst : block.instructions()) {
                const bool isTerminator = inst.op == IROp::Branch || inst.op == IROp::CondBranch ||
                                          inst.op == IROp::Return || inst.op == IROp::Store ||
                                          inst.op == IROp::GlobalStore || inst.op == IROp::Call;

                if (isTerminator) {
                    live.push_back(std::move(inst));
                    continue;
                }

                if (inst.op == IROp::Const && inst.result.has_value() && inst.immediate == 0 &&
                    inst.operands.empty()) {
                    continue;
                }

                live.push_back(std::move(inst));
            }

            block.instructions() = std::move(live);
        }
    }
}

} // namespace toyc::optimizer
