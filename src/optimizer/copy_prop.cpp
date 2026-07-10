#include "optimizer/copy_prop.h"

#include "ir/basic_block.h"

#include <unordered_map>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

int resolveAlias(const std::unordered_map<int, int>& aliases, int id) {
    int current = id;
    std::unordered_map<int, int>::const_iterator it = aliases.find(current);
    while (it != aliases.end() && it->second != current) {
        current = it->second;
        it = aliases.find(current);
    }
    return current;
}

} // namespace

void CopyPropPass::run(IRModule& module) {
    for (auto& function : module.functions) {
        for (auto& block : function.blocks) {
            std::unordered_map<int, int> slotValues;
            std::unordered_map<int, int> aliases;

            std::vector<IRInstruction> kept;
            kept.reserve(block.instructions().size());

            for (auto& inst : block.instructions()) {
                for (IRValue& operand : inst.operands) {
                    if (operand.id >= 0) {
                        operand.id = resolveAlias(aliases, operand.id);
                    }
                }

                if (inst.op == IROp::Store && inst.operands.size() >= 2) {
                    slotValues[inst.operands[1].id] = inst.operands[0].id;
                    kept.push_back(std::move(inst));
                    continue;
                }

                if (inst.op == IROp::Load && inst.result.has_value() && !inst.operands.empty()) {
                    const auto it = slotValues.find(inst.operands[0].id);
                    if (it != slotValues.end()) {
                        aliases[inst.result->id] = resolveAlias(aliases, it->second);
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
