#include "optimizer/constant_fold.h"

#include "ir/basic_block.h"

#include <optional>
#include <unordered_map>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

std::optional<int> constantOf(const std::unordered_map<int, int>& constants, const IRValue& value) {
    const auto it = constants.find(value.id);
    if (it == constants.end()) {
        return std::nullopt;
    }
    return it->second;
}

void replaceWithConst(IRInstruction& inst, int value, std::unordered_map<int, int>& constants) {
    inst.op = IROp::Const;
    inst.immediate = value;
    inst.operands.clear();
    if (inst.result.has_value()) {
        constants[inst.result->id] = value;
    }
}

void foldGlobalConstants(IRFunction& function, const IRModule& module) {
    std::unordered_map<std::string, int> globalValues;
    for (const IRGlobal& global : module.globals) {
        if (global.isConst) {
            globalValues.emplace(global.name, global.initValue);
        }
    }

    for (auto& block : function.blocks) {
        for (auto& inst : block.instructions()) {
            if (inst.op != IROp::GlobalLoad || !inst.result.has_value()) {
                continue;
            }
            const auto it = globalValues.find(inst.callee);
            if (it == globalValues.end()) {
                continue;
            }
            inst.op = IROp::Const;
            inst.immediate = it->second;
            inst.callee.clear();
            inst.operands.clear();
        }
    }
}

} // namespace

void ConstantFoldPass::run(toyc::ir::IRModule& module) {
    for (auto& function : module.functions) {
        foldGlobalConstants(function, module);

        std::unordered_map<int, int> constants;
        for (auto& block : function.blocks) {
            for (auto& inst : block.instructions()) {
                if (!inst.result.has_value()) {
                    continue;
                }

                if (inst.op == IROp::Const) {
                    constants[inst.result->id] = inst.immediate;
                    continue;
                }

                if (inst.operands.size() == 1) {
                    const std::optional<int> value = constantOf(constants, inst.operands[0]);
                    if (!value.has_value()) {
                        continue;
                    }
                    if (inst.op == IROp::Neg) {
                        replaceWithConst(inst, -*value, constants);
                    } else if (inst.op == IROp::Not) {
                        replaceWithConst(inst, *value == 0 ? 1 : 0, constants);
                    }
                    continue;
                }

                if (inst.operands.size() != 2) {
                    continue;
                }

                const std::optional<int> lhsConst = constantOf(constants, inst.operands[0]);
                const std::optional<int> rhsConst = constantOf(constants, inst.operands[1]);

                if (inst.op == IROp::Mul &&
                    ((lhsConst.has_value() && *lhsConst == 0) || (rhsConst.has_value() && *rhsConst == 0))) {
                    replaceWithConst(inst, 0, constants);
                    continue;
                }

                if (lhsConst == std::nullopt || rhsConst == std::nullopt) {
                    continue;
                }

                const int lhsValue = *lhsConst;
                const int rhsValue = *rhsConst;
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
                } else if (inst.op == IROp::ICmpEq) {
                    folded = lhsValue == rhsValue ? 1 : 0;
                } else if (inst.op == IROp::ICmpNe) {
                    folded = lhsValue != rhsValue ? 1 : 0;
                } else if (inst.op == IROp::ICmpLt) {
                    folded = lhsValue < rhsValue ? 1 : 0;
                } else if (inst.op == IROp::ICmpLe) {
                    folded = lhsValue <= rhsValue ? 1 : 0;
                } else if (inst.op == IROp::ICmpGt) {
                    folded = lhsValue > rhsValue ? 1 : 0;
                } else if (inst.op == IROp::ICmpGe) {
                    folded = lhsValue >= rhsValue ? 1 : 0;
                }

                if (folded.has_value()) {
                    replaceWithConst(inst, *folded, constants);
                }
            }
        }
    }
}

} // namespace toyc::optimizer
