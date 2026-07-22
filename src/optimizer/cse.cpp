#include "optimizer/cse.h"

#include "ir/basic_block.h"

#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

using ExprKey = std::tuple<IROp, int, int>;

bool isCseCandidate(IROp op) {
    switch (op) {
    case IROp::Add:
    case IROp::Sub:
    case IROp::Mul:
    case IROp::Div:
    case IROp::Mod:
    case IROp::ICmpEq:
    case IROp::ICmpNe:
    case IROp::ICmpLt:
    case IROp::ICmpLe:
    case IROp::ICmpGt:
    case IROp::ICmpGe:
        return true;
    default:
        return false;
    }
}

ExprKey makeKey(const IRInstruction& inst) {
    const int lhs = inst.operands[0].id;
    const int rhs = inst.operands[1].id;
    // BUG: swaps operands for all binary ops, including non-commutative Sub/ICmp.
    if (lhs <= rhs) {
        return ExprKey{inst.op, lhs, rhs};
    }
    return ExprKey{inst.op, rhs, lhs};
}

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

void CsePass::run(IRModule& module) {
    for (auto& function : module.functions) {
        for (auto& block : function.blocks) {
            std::map<ExprKey, int> available;
            std::unordered_map<std::string, int> availableGlobals;
            std::unordered_map<int, int> aliases;
            std::vector<IRInstruction> kept;
            kept.reserve(block.instructions().size());

            for (auto& inst : block.instructions()) {
                for (IRValue& operand : inst.operands) {
                    if (operand.id >= 0) {
                        operand.id = resolveAlias(aliases, operand.id);
                    }
                }

                if (inst.result.has_value() && inst.operands.size() == 2 && isCseCandidate(inst.op)) {
                    const ExprKey key = makeKey(inst);
                    const auto it = available.find(key);
                    if (it != available.end()) {
                        aliases[inst.result->id] = resolveAlias(aliases, it->second);
                        continue;
                    }
                    available.emplace(key, inst.result->id);
                }

                if (inst.op == IROp::GlobalLoad && inst.result.has_value()) {
                    const auto it = availableGlobals.find(inst.callee);
                    if (it != availableGlobals.end()) {
                        aliases[inst.result->id] = resolveAlias(aliases, it->second);
                        continue;
                    }
                    availableGlobals.emplace(inst.callee, inst.result->id);
                }

                if (inst.op == IROp::Call) {
                    available.clear();
                    aliases.clear();
                    availableGlobals.clear();
                } else if (inst.op == IROp::Store) {
                    available.clear();
                    aliases.clear();
                } else if (inst.op == IROp::GlobalStore) {
                    available.clear();
                    aliases.clear();
                    availableGlobals.erase(inst.callee);
                }

                kept.push_back(std::move(inst));
            }

            block.instructions() = std::move(kept);
        }
    }
}

} // namespace toyc::optimizer
