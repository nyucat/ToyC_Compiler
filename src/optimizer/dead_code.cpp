#include "optimizer/dead_code.h"

#include "ir/basic_block.h"

#include <unordered_set>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

bool hasSideEffects(const IRInstruction& inst) {
    switch (inst.op) {
    case IROp::Store:
    case IROp::GlobalStore:
    case IROp::Call:
    case IROp::Branch:
    case IROp::CondBranch:
    case IROp::Return:
        return true;
    default:
        return false;
    }
}

void collectUses(const IRInstruction& inst, std::unordered_set<int>& uses) {
    for (const IRValue& operand : inst.operands) {
        if (operand.id >= 0) {
            uses.insert(operand.id);
        }
    }
}

} // namespace

void DeadCodeEliminationPass::run(IRModule& module) {
    for (auto& function : module.functions) {
        // Build use set once, then incrementally update
        std::unordered_set<int> uses;
        // Pre-scan to build initial use set
        for (const auto& block : function.blocks) {
            for (const auto& inst : block.instructions()) {
                collectUses(inst, uses);
            }
        }

        bool changed = true;
        while (changed) {
            changed = false;

            for (auto& block : function.blocks) {
                std::vector<IRInstruction> kept;
                kept.reserve(block.instructions().size());

                for (auto& inst : block.instructions()) {
                    // Remove useless Const without result
                    if (!inst.result.has_value() && inst.op == IROp::Const && inst.operands.empty()) {
                        changed = true;
                        continue;
                    }
                    // Remove dead instructions: has result, no side effects, result unused
                    if (inst.result.has_value() && inst.result->id >= 0 && !hasSideEffects(inst) &&
                        uses.find(inst.result->id) == uses.end()) {
                        // Remove this instruction's operands from use set
                        for (const IRValue& operand : inst.operands) {
                            if (operand.id >= 0) {
                                uses.erase(operand.id);
                            }
                        }
                        changed = true;
                        continue;
                    }
                    kept.push_back(std::move(inst));
                }

                block.instructions() = std::move(kept);
            }
        }
    }
}

} // namespace toyc::optimizer
