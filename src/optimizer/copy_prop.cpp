#include "optimizer/copy_prop.h"

#include "ir/basic_block.h"

#include <unordered_map>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

void replaceAllUses(IRFunction& function, int fromId, int toId) {
    if (fromId == toId) {
        return;
    }

    for (auto& block : function.blocks) {
        for (auto& inst : block.instructions()) {
            for (IRValue& operand : inst.operands) {
                if (operand.id == fromId) {
                    operand.id = toId;
                }
            }
            if (inst.result.has_value() && inst.result->id == fromId) {
                inst.result->id = toId;
            }
        }
    }
}

} // namespace

void CopyPropPass::run(IRModule& module) {
    for (auto& function : module.functions) {
        for (auto& block : function.blocks) {
            std::unordered_map<int, int> slotValues;

            std::vector<IRInstruction> kept;
            kept.reserve(block.instructions().size());

            for (auto& inst : block.instructions()) {
                if (inst.op == IROp::Store && inst.operands.size() >= 2) {
                    slotValues[inst.operands[1].id] = inst.operands[0].id;
                    kept.push_back(std::move(inst));
                    continue;
                }

                if (inst.op == IROp::Load && inst.result.has_value() && !inst.operands.empty()) {
                    const auto it = slotValues.find(inst.operands[0].id);
                    if (it != slotValues.end()) {
                        replaceAllUses(function, inst.result->id, it->second);
                        continue;
                    }
                }

                kept.push_back(std::move(inst));
            }

            block.instructions() = std::move(kept);
        }
    }
}

} // namespace toyc::optimizer
