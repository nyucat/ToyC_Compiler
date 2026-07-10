#include "optimizer/cse.h"

#include "ir/basic_block.h"

#include <map>
#include <tuple>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

using ExprKey = std::tuple<IROp, int, int>;

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
    if (lhs <= rhs) {
        return ExprKey{inst.op, lhs, rhs};
    }
    return ExprKey{inst.op, rhs, lhs};
}

} // namespace

void CsePass::run(IRModule& module) {
    for (auto& function : module.functions) {
        for (auto& block : function.blocks) {
            std::map<ExprKey, int> available;
            std::vector<IRInstruction> kept;
            kept.reserve(block.instructions().size());

            for (auto& inst : block.instructions()) {
                if (inst.result.has_value() && inst.operands.size() == 2 && isCseCandidate(inst.op)) {
                    const ExprKey key = makeKey(inst);
                    const auto it = available.find(key);
                    if (it != available.end()) {
                        replaceAllUses(function, inst.result->id, it->second);
                        continue;
                    }
                    available.emplace(key, inst.result->id);
                }

                if (inst.op == IROp::Call || inst.op == IROp::Store || inst.op == IROp::GlobalStore) {
                    available.clear();
                }

                kept.push_back(std::move(inst));
            }

            block.instructions() = std::move(kept);
        }
    }
}

} // namespace toyc::optimizer
