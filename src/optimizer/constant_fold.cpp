#include "optimizer/constant_fold.h"

#include "ir/basic_block.h"

#include <optional>
#include <unordered_map>

namespace toyc::optimizer {

void ConstantFoldPass::run(toyc::ir::IRModule& module) {
    using namespace toyc::ir;

    for (auto& function : module.functions) {
        std::unordered_map<int, int> constants;
        for (auto& block : function.blocks) {
            constants.clear();
            for (auto& inst : block.instructions()) {
                if (!inst.result.has_value()) {
                    continue;
                }

                if (inst.op == IROp::Const) {
                    constants[inst.result->id] = inst.immediate;
                    continue;
                }

                if (inst.operands.size() != 2) {
                    continue;
                }

                const auto lhsIt = constants.find(inst.operands[0].id);
                const auto rhsIt = constants.find(inst.operands[1].id);

                if (inst.op == IROp::Mul &&
                    ((lhsIt != constants.end() && lhsIt->second == 0) ||
                     (rhsIt != constants.end() && rhsIt->second == 0))) {
                    inst.op = IROp::Const;
                    inst.immediate = 0;
                    inst.operands.clear();
                    constants[inst.result->id] = 0;
                    continue;
                }

                if (lhsIt == constants.end() || rhsIt == constants.end()) {
                    continue;
                }

                const int lhsValue = lhsIt->second;
                const int rhsValue = rhsIt->second;
                std::optional<int> folded;

                if (inst.op == IROp::Add) {
                    folded = lhsValue + rhsValue;
                } else if (inst.op == IROp::Sub) {
                    folded = lhsValue - rhsValue;
                } else if (inst.op == IROp::Mul) {
                    folded = lhsValue * rhsValue;
                } else if (inst.op == IROp::Div && rhsValue != 0) {
                    folded = lhsValue / rhsValue;
                } else if (inst.op == IROp::Mod && rhsValue != 0) {
                    folded = lhsValue % rhsValue;
                }

                if (folded.has_value()) {
                    inst.op = IROp::Const;
                    inst.immediate = *folded;
                    inst.operands.clear();
                    constants[inst.result->id] = *folded;
                }
            }
        }
    }
}

} // namespace toyc::optimizer
