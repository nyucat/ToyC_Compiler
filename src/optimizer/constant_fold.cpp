#include "optimizer/constant_fold.h"

#include "ir/basic_block.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

struct LinearExpr {
    bool valid = false;
    int base = -1;
    int coeff = 0;
    int constant = 0;
};

std::optional<int> constantOf(const std::unordered_map<int, int>& constants, const IRValue& value) {
    const auto it = constants.find(value.id);
    if (it == constants.end()) {
        return std::nullopt;
    }
    return it->second;
}

int resolveAlias(std::unordered_map<int, int>& aliases, int id) {
    int current = id;
    auto it = aliases.find(current);
    while (it != aliases.end() && it->second != current) {
        current = it->second;
        it = aliases.find(current);
    }
    return current;
}

std::optional<int> constantOf(
    const std::unordered_map<int, int>& constants,
    std::unordered_map<int, int>& aliases,
    const IRValue& value
) {
    return constantOf(constants, IRValue{resolveAlias(aliases, value.id), value.type});
}

LinearExpr linearOf(
    const std::unordered_map<int, LinearExpr>& linear,
    std::unordered_map<int, int>& aliases,
    const std::unordered_map<int, int>& constants,
    const IRValue& value
) {
    const int id = resolveAlias(aliases, value.id);
    const auto linIt = linear.find(id);
    if (linIt != linear.end()) {
        return linIt->second;
    }
    const auto constIt = constants.find(id);
    if (constIt != constants.end()) {
        return LinearExpr{true, -1, 0, constIt->second};
    }
    return LinearExpr{true, id, 1, 0};
}

std::optional<LinearExpr> combineLinear(const LinearExpr& lhs, const LinearExpr& rhs, int sign) {
    if (!lhs.valid || !rhs.valid) {
        return std::nullopt;
    }
    if (lhs.base >= 0 && rhs.base >= 0 && lhs.base != rhs.base) {
        return std::nullopt;
    }

    LinearExpr result;
    result.valid = true;
    result.base = lhs.base >= 0 ? lhs.base : rhs.base;
    result.coeff = lhs.coeff + sign * rhs.coeff;
    result.constant = lhs.constant + sign * rhs.constant;
    if (result.coeff == 0) {
        result.base = -1;
    }
    return result;
}

std::optional<LinearExpr> scaleLinear(const LinearExpr& value, int factor) {
    if (!value.valid) {
        return std::nullopt;
    }
    return LinearExpr{true, value.base, value.coeff * factor, value.constant * factor};
}

void replaceWithConst(IRInstruction& inst, int value, std::unordered_map<int, int>& constants) {
    inst.op = IROp::Const;
    inst.immediate = value;
    inst.operands.clear();
    if (inst.result.has_value()) {
        constants[inst.result->id] = value;
    }
}

void aliasResult(
    const IRInstruction& inst,
    int sourceId,
    std::unordered_map<int, int>& aliases,
    std::unordered_map<int, int>& constants
) {
    if (!inst.result.has_value()) {
        return;
    }
    const int resolved = resolveAlias(aliases, sourceId);
    aliases[inst.result->id] = resolved;

    const auto constIt = constants.find(resolved);
    if (constIt != constants.end()) {
        constants[inst.result->id] = constIt->second;
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
            std::unordered_map<int, int> aliases;
            std::unordered_map<int, LinearExpr> linear;
            std::vector<IRInstruction> kept;
            kept.reserve(block.instructions().size());

            for (auto& inst : block.instructions()) {
                for (IRValue& operand : inst.operands) {
                    if (operand.id >= 0) {
                        operand.id = resolveAlias(aliases, operand.id);
                    }
                }

                if (!inst.result.has_value()) {
                    kept.push_back(std::move(inst));
                    continue;
                }

                if (inst.op == IROp::Const) {
                    constants[inst.result->id] = inst.immediate;
                    linear[inst.result->id] = LinearExpr{true, -1, 0, inst.immediate};
                    kept.push_back(std::move(inst));
                    continue;
                }

                if (inst.operands.size() == 1) {
                    const std::optional<int> value = constantOf(constants, aliases, inst.operands[0]);
                    if (!value.has_value()) {
                        kept.push_back(std::move(inst));
                        continue;
                    }
                    if (inst.op == IROp::Neg) {
                        replaceWithConst(inst, -*value, constants);
                    } else if (inst.op == IROp::Not) {
                        replaceWithConst(inst, *value == 0 ? 1 : 0, constants);
                    }
                    if (inst.op == IROp::Const) {
                        linear[inst.result->id] = LinearExpr{true, -1, 0, inst.immediate};
                    }
                    kept.push_back(std::move(inst));
                    continue;
                }

                if (inst.operands.size() != 2) {
                    kept.push_back(std::move(inst));
                    continue;
                }

                const std::optional<int> lhsConst = constantOf(constants, aliases, inst.operands[0]);
                const std::optional<int> rhsConst = constantOf(constants, aliases, inst.operands[1]);

                if (inst.op == IROp::Mul &&
                    ((lhsConst.has_value() && *lhsConst == 0) || (rhsConst.has_value() && *rhsConst == 0))) {
                    replaceWithConst(inst, 0, constants);
                    kept.push_back(std::move(inst));
                    continue;
                }

                if ((inst.op == IROp::Add || inst.op == IROp::Sub) &&
                    rhsConst.has_value() && *rhsConst == 0) {
                    aliasResult(inst, inst.operands[0].id, aliases, constants);
                    linear[inst.result->id] = linearOf(linear, aliases, constants, inst.operands[0]);
                    continue;
                }

                if (inst.op == IROp::Add && lhsConst.has_value() && *lhsConst == 0) {
                    aliasResult(inst, inst.operands[1].id, aliases, constants);
                    linear[inst.result->id] = linearOf(linear, aliases, constants, inst.operands[1]);
                    continue;
                }

                if (inst.op == IROp::Sub && inst.operands[0].id == inst.operands[1].id) {
                    replaceWithConst(inst, 0, constants);
                    linear[inst.result->id] = LinearExpr{true, -1, 0, 0};
                    kept.push_back(std::move(inst));
                    continue;
                }

                if (inst.op == IROp::Mul && rhsConst.has_value() && *rhsConst == 1) {
                    aliasResult(inst, inst.operands[0].id, aliases, constants);
                    linear[inst.result->id] = linearOf(linear, aliases, constants, inst.operands[0]);
                    continue;
                }

                if (inst.op == IROp::Mul && lhsConst.has_value() && *lhsConst == 1) {
                    aliasResult(inst, inst.operands[1].id, aliases, constants);
                    linear[inst.result->id] = linearOf(linear, aliases, constants, inst.operands[1]);
                    continue;
                }

                if (inst.op == IROp::Div && rhsConst.has_value() && *rhsConst == 1) {
                    aliasResult(inst, inst.operands[0].id, aliases, constants);
                    linear[inst.result->id] = linearOf(linear, aliases, constants, inst.operands[0]);
                    continue;
                }

                if (inst.op == IROp::Mod && rhsConst.has_value() && *rhsConst == 1) {
                    replaceWithConst(inst, 0, constants);
                    linear[inst.result->id] = LinearExpr{true, -1, 0, 0};
                    kept.push_back(std::move(inst));
                    continue;
                }

                std::optional<LinearExpr> foldedLinear;
                if (inst.op == IROp::Add || inst.op == IROp::Sub) {
                    foldedLinear = combineLinear(
                        linearOf(linear, aliases, constants, inst.operands[0]),
                        linearOf(linear, aliases, constants, inst.operands[1]),
                        inst.op == IROp::Add ? 1 : -1
                    );
                } else if (inst.op == IROp::Mul) {
                    if (rhsConst.has_value()) {
                        foldedLinear = scaleLinear(
                            linearOf(linear, aliases, constants, inst.operands[0]),
                            *rhsConst
                        );
                    } else if (lhsConst.has_value()) {
                        foldedLinear = scaleLinear(
                            linearOf(linear, aliases, constants, inst.operands[1]),
                            *lhsConst
                        );
                    }
                } else if (inst.op == IROp::Div && rhsConst.has_value() && *rhsConst != 0) {
                    LinearExpr lhsLinear = linearOf(linear, aliases, constants, inst.operands[0]);
                    if (lhsLinear.valid && lhsLinear.coeff % *rhsConst == 0 &&
                        lhsLinear.constant % *rhsConst == 0) {
                        foldedLinear = LinearExpr{
                            true,
                            lhsLinear.base,
                            lhsLinear.coeff / *rhsConst,
                            lhsLinear.constant / *rhsConst
                        };
                    }
                }

                if (foldedLinear.has_value()) {
                    linear[inst.result->id] = *foldedLinear;
                    if (foldedLinear->coeff == 0) {
                        replaceWithConst(inst, foldedLinear->constant, constants);
                        kept.push_back(std::move(inst));
                        continue;
                    }
                    if (foldedLinear->coeff == 1 && foldedLinear->constant == 0 &&
                        foldedLinear->base >= 0) {
                        aliasResult(inst, foldedLinear->base, aliases, constants);
                        continue;
                    }
                }

                if (lhsConst == std::nullopt || rhsConst == std::nullopt) {
                    kept.push_back(std::move(inst));
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
                    linear[inst.result->id] = LinearExpr{true, -1, 0, *folded};
                }
                kept.push_back(std::move(inst));
            }

            block.instructions() = std::move(kept);
        }
    }
}

} // namespace toyc::optimizer
