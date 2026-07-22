#include "optimizer/copy_prop.h"

#include "ir/basic_block.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
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

std::unordered_set<int> collectLocalSlots(const IRFunction& function) {
    std::unordered_set<int> slots;
    for (const auto& block : function.blocks) {
        for (const IRInstruction& inst : block.instructions()) {
            if (inst.op == IROp::Alloca && inst.result.has_value()) {
                slots.insert(inst.result->id);
            }
        }
    }
    return slots;
}

bool isLocalSlot(const std::unordered_set<int>& localSlots, const IRValue& value) {
    return value.id >= 0 && localSlots.find(value.id) != localSlots.end();
}

bool removeOverwrittenLocalStores(IRFunction& function) {
    const std::unordered_set<int> localSlots = collectLocalSlots(function);
    if (localSlots.empty()) {
        return false;
    }

    bool changed = false;
    for (auto& block : function.blocks) {
        std::unordered_set<int> liveSlotsAtPoint;
        if (!block.successors().empty()) {
            liveSlotsAtPoint = localSlots;
        }

        std::vector<IRInstruction> reversed;
        reversed.reserve(block.instructions().size());

        for (auto it = block.instructions().rbegin(); it != block.instructions().rend(); ++it) {
            IRInstruction& inst = *it;

            if (inst.op == IROp::Load && !inst.operands.empty() && isLocalSlot(localSlots, inst.operands[0])) {
                liveSlotsAtPoint.insert(inst.operands[0].id);
                reversed.push_back(std::move(inst));
                continue;
            }

            if (inst.op == IROp::Store && inst.operands.size() >= 2 &&
                isLocalSlot(localSlots, inst.operands[1])) {
                const int slotId = inst.operands[1].id;
                if (liveSlotsAtPoint.find(slotId) == liveSlotsAtPoint.end()) {
                    changed = true;
                    continue;
                }
                liveSlotsAtPoint.erase(slotId);
                reversed.push_back(std::move(inst));
                continue;
            }

            if (inst.op == IROp::Call) {
                liveSlotsAtPoint = localSlots;
            }

            reversed.push_back(std::move(inst));
        }

        std::reverse(reversed.begin(), reversed.end());
        block.instructions() = std::move(reversed);
    }

    return changed;
}

} // namespace

void CopyPropPass::run(IRModule& module) {
    for (auto& function : module.functions) {
        buildCFG(function);
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

                if (inst.op == IROp::Move && inst.result.has_value() && !inst.operands.empty()) {
                    aliases[inst.result->id] = resolveAlias(aliases, inst.operands[0].id);
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
        if (removeOverwrittenLocalStores(function)) {
            buildCFG(function);
        }
    }
}

} // namespace toyc::optimizer
