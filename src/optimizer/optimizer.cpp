#include "optimizer/optimizer.h"

#include "optimizer/cfg_simplify.h"
#include "optimizer/constant_fold.h"
#include "optimizer/copy_prop.h"
#include "optimizer/cse.h"
#include "optimizer/dead_code.h"

#include "ir/basic_block.h"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

int wrapAdd(int lhs, int rhs) {
    return static_cast<int>(static_cast<std::int32_t>(
        static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs)));
}

int wrapSub(int lhs, int rhs) {
    return static_cast<int>(static_cast<std::int32_t>(
        static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs)));
}

int wrapMul(int lhs, int rhs) {
    return static_cast<int>(static_cast<std::int32_t>(
        static_cast<std::uint32_t>(lhs) * static_cast<std::uint32_t>(rhs)));
}

void replaceFunctionWithConstReturn(IRFunction& function, int value) {
    const std::string label = function.blocks.empty() ? function.name + ".entry.0" : function.blocks.front().label();
    function.blocks.clear();
    function.blocks.emplace_back(label);

    IRValue result{function.nextReg++, IRType::I32};
    IRInstruction c;
    c.op = IROp::Const;
    c.result = result;
    c.immediate = value;
    function.blocks.front().instructions().push_back(std::move(c));

    IRInstruction ret;
    ret.op = IROp::Return;
    ret.operands = {result};
    function.blocks.front().instructions().push_back(std::move(ret));
    buildCFG(function);
}

bool isSimpleInlineCandidate(const IRFunction& function) {
    if (function.blocks.size() != 1) {
        return false;
    }

    const auto& insts = function.blocks.front().instructions();
    if (insts.empty() || insts.back().op != IROp::Return) {
        return false;
    }

    for (std::size_t i = 0; i + 1 < insts.size(); ++i) {
        switch (insts[i].op) {
        case IROp::Branch:
        case IROp::CondBranch:
        case IROp::Call:
        case IROp::Return:
        case IROp::GlobalStore:
            return false;
        default:
            break;
        }
    }
    return insts.size() <= 24;
}

IRValue remapValue(const IRValue& value, const std::unordered_map<int, IRValue>& valueMap) {
    const auto it = valueMap.find(value.id);
    if (it == valueMap.end()) {
        return value;
    }
    return it->second;
}

bool inlineCallIntoBlock(IRFunction& caller, const IRFunction& callee, IRInstruction& call,
                         std::vector<IRInstruction>& output) {
    if (!isSimpleInlineCandidate(callee) || call.operands.size() != callee.paramNames.size()) {
        return false;
    }

    std::unordered_map<int, IRValue> valueMap;
    std::unordered_map<int, int> paramValueIds;

    const auto& calleeInsts = callee.blocks.front().instructions();
    for (const IRInstruction& inst : calleeInsts) {
        if (inst.op == IROp::ParamLoad && inst.result.has_value()) {
            if (inst.immediate < 0 || static_cast<std::size_t>(inst.immediate) >= call.operands.size()) {
                return false;
            }
            valueMap[inst.result->id] = call.operands[static_cast<std::size_t>(inst.immediate)];
            paramValueIds[inst.result->id] = inst.immediate;
        }
    }

    for (const IRInstruction& inst : calleeInsts) {
        if (inst.op != IROp::Store || inst.operands.size() < 2) {
            continue;
        }
        const auto paramIt = paramValueIds.find(inst.operands[0].id);
        if (paramIt != paramValueIds.end()) {
            valueMap[inst.operands[1].id] = call.operands[static_cast<std::size_t>(paramIt->second)];
        }
    }

    IRValue returnValue{-1, IRType::Void};
    bool hasReturnValue = false;

    for (const IRInstruction& inst : calleeInsts) {
        if (inst.op == IROp::Alloca || inst.op == IROp::ParamLoad) {
            continue;
        }
        if (inst.op == IROp::Store) {
            continue;
        }
        if (inst.op == IROp::Load && inst.result.has_value() && !inst.operands.empty()) {
            const auto it = valueMap.find(inst.operands[0].id);
            if (it != valueMap.end()) {
                valueMap[inst.result->id] = it->second;
                continue;
            }
        }
        if (inst.op == IROp::Return) {
            if (!inst.operands.empty()) {
                returnValue = remapValue(inst.operands[0], valueMap);
                hasReturnValue = true;
            }
            break;
        }

        IRInstruction cloned = inst;
        for (IRValue& operand : cloned.operands) {
            operand = remapValue(operand, valueMap);
        }
        if (cloned.result.has_value()) {
            IRValue newResult{caller.nextReg++, cloned.result->type};
            valueMap[cloned.result->id] = newResult;
            cloned.result = newResult;
        }
        output.push_back(std::move(cloned));
    }

    if (call.result.has_value() && hasReturnValue) {
        IRInstruction move;
        move.op = IROp::Move;
        move.result = call.result;
        move.operands = {returnValue};
        output.push_back(std::move(move));
    }
    return true;
}

void inlineSmallFunctions(IRModule& module) {
    std::unordered_map<std::string, const IRFunction*> functions;
    for (const IRFunction& function : module.functions) {
        functions.emplace(function.name, &function);
    }

    for (IRFunction& caller : module.functions) {
        for (auto& block : caller.blocks) {
            std::vector<IRInstruction> rewritten;
            rewritten.reserve(block.instructions().size());

            for (auto& inst : block.instructions()) {
                if (inst.op == IROp::Call && inst.callee != caller.name) {
                    const auto it = functions.find(inst.callee);
                    if (it != functions.end() && inlineCallIntoBlock(caller, *it->second, inst, rewritten)) {
                        continue;
                    }
                }
                rewritten.push_back(std::move(inst));
            }

            block.instructions() = std::move(rewritten);
        }
    }
}

void eliminateTailRecursion(IRModule& module) {
    for (IRFunction& function : module.functions) {
        if (function.blocks.empty() || function.paramNames.empty()) {
            continue;
        }

        bool hasTailCall = false;
        for (auto& block : function.blocks) {
            auto& insts = block.instructions();
            if (insts.size() < 2) {
                continue;
            }
            const IRInstruction& ret = insts.back();
            const IRInstruction& call = insts[insts.size() - 2];
            if (ret.op == IROp::Return && call.op == IROp::Call && call.callee == function.name &&
                call.result.has_value() && !ret.operands.empty() && ret.operands[0].id == call.result->id &&
                call.operands.size() == function.paramNames.size()) {
                hasTailCall = true;
                break;
            }
        }
        if (!hasTailCall) {
            continue;
        }

        std::unordered_map<int, IRValue> paramSlots;
        const auto& entryInsts = function.blocks.front().instructions();
        std::size_t prologueEnd = 0;
        for (const IRInstruction& inst : entryInsts) {
            if (inst.op != IROp::Store || inst.operands.size() < 2) {
                ++prologueEnd;
                continue;
            }
            const int valueId = inst.operands[0].id;
            for (const IRInstruction& maybeParam : entryInsts) {
                if (maybeParam.op == IROp::ParamLoad && maybeParam.result.has_value() &&
                    maybeParam.result->id == valueId && maybeParam.immediate >= 0 &&
                    static_cast<std::size_t>(maybeParam.immediate) < function.paramNames.size()) {
                    paramSlots[maybeParam.immediate] = inst.operands[1];
                    break;
                }
            }
            ++prologueEnd;
            if (paramSlots.size() == function.paramNames.size()) {
                break;
            }
        }

        if (paramSlots.size() != function.paramNames.size()) {
            continue;
        }

        const std::string loopLabel = function.name + ".tail.loop." + std::to_string(function.nextBlockId++);
        {
            BasicBlock loopBlock(loopLabel);
            auto& entryBody = function.blocks.front().instructions();
            for (std::size_t i = prologueEnd; i < entryBody.size(); ++i) {
                loopBlock.instructions().push_back(std::move(entryBody[i]));
            }
            entryBody.erase(entryBody.begin() + static_cast<std::ptrdiff_t>(prologueEnd), entryBody.end());

            IRInstruction jumpToLoop;
            jumpToLoop.op = IROp::Branch;
            jumpToLoop.label = loopLabel;
            entryBody.push_back(std::move(jumpToLoop));
            function.blocks.insert(function.blocks.begin() + 1, std::move(loopBlock));
        }

        for (auto& block : function.blocks) {
            auto& insts = block.instructions();
            if (insts.size() < 2) {
                continue;
            }

            IRInstruction ret = insts.back();
            IRInstruction call = insts[insts.size() - 2];
            if (ret.op != IROp::Return || call.op != IROp::Call || call.callee != function.name ||
                !call.result.has_value() || ret.operands.empty() ||
                ret.operands[0].id != call.result->id ||
                call.operands.size() != function.paramNames.size()) {
                continue;
            }

            insts.pop_back();
            insts.pop_back();

            std::vector<IRValue> copies;
            copies.reserve(call.operands.size());
            for (IRValue arg : call.operands) {
                IRValue temp{function.nextReg++, arg.type};
                IRInstruction move;
                move.op = IROp::Move;
                move.result = temp;
                move.operands = {arg};
                insts.push_back(std::move(move));
                copies.push_back(temp);
            }

            for (std::size_t i = 0; i < copies.size(); ++i) {
                IRInstruction store;
                store.op = IROp::Store;
                store.operands = {copies[i], paramSlots[static_cast<int>(i)]};
                insts.push_back(std::move(store));
            }

            IRInstruction branch;
            branch.op = IROp::Branch;
            branch.label = loopLabel;
            insts.push_back(std::move(branch));
        }

        buildCFG(function);
    }
}

std::optional<int> constValueOf(const std::unordered_map<int, int>& constants, const IRValue& value) {
    const auto it = constants.find(value.id);
    if (it == constants.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<int> loadSlotOf(const IRInstruction& inst) {
    if (inst.op != IROp::Load || !inst.result.has_value() || inst.operands.empty()) {
        return std::nullopt;
    }
    return inst.operands[0].id;
}

std::optional<int> addChainConstant(
    int valueId,
    int baseValueId,
    const std::unordered_map<int, IRInstruction>& defs,
    const std::unordered_map<int, int>& constants
) {
    if (valueId == baseValueId) {
        return 0;
    }

    const auto defIt = defs.find(valueId);
    if (defIt == defs.end() || defIt->second.op != IROp::Add || defIt->second.operands.size() != 2) {
        return std::nullopt;
    }

    const IRInstruction& add = defIt->second;
    if (const auto rhsConst = constValueOf(constants, add.operands[1])) {
        if (const auto lhs = addChainConstant(add.operands[0].id, baseValueId, defs, constants)) {
            return *lhs + *rhsConst;
        }
    }
    if (const auto lhsConst = constValueOf(constants, add.operands[0])) {
        if (const auto rhs = addChainConstant(add.operands[1].id, baseValueId, defs, constants)) {
            return *rhs + *lhsConst;
        }
    }
    return std::nullopt;
}

bool replaceCountedModuloLoopWithConst(IRFunction& function) {
    std::unordered_map<int, int> constants;
    std::unordered_map<int, IRInstruction> defs;
    for (const auto& block : function.blocks) {
        for (const IRInstruction& inst : block.instructions()) {
            if (inst.result.has_value()) {
                defs[inst.result->id] = inst;
                if (inst.op == IROp::Const) {
                    constants[inst.result->id] = inst.immediate;
                }
            }
            if (inst.op == IROp::Call || inst.op == IROp::GlobalStore) {
                return false;
            }
        }
    }

    for (const BasicBlock& condBlock : function.blocks) {
        const IRInstruction* condTerm = condBlock.terminator();
        if (condTerm == nullptr || condTerm->op != IROp::CondBranch || condTerm->operands.empty()) {
            continue;
        }

        const auto cmpIt = defs.find(condTerm->operands[0].id);
        if (cmpIt == defs.end() || cmpIt->second.op != IROp::ICmpLt || cmpIt->second.operands.size() != 2) {
            continue;
        }

        const auto iLoadIt = defs.find(cmpIt->second.operands[0].id);
        if (iLoadIt == defs.end()) {
            continue;
        }
        const auto iSlot = loadSlotOf(iLoadIt->second);
        const auto limit = constValueOf(constants, cmpIt->second.operands[1]);
        if (!iSlot || !limit) {
            continue;
        }

        BasicBlock* body = nullptr;
        BasicBlock* end = nullptr;
        for (auto& block : function.blocks) {
            if (block.label() == condTerm->trueLabel) {
                body = &block;
            }
            if (block.label() == condTerm->falseLabel) {
                end = &block;
            }
        }
        if (body == nullptr || end == nullptr || body->terminator() == nullptr ||
            body->terminator()->op != IROp::Branch || body->terminator()->label != condBlock.label()) {
            continue;
        }

        int iInit = 0;
        int sInit = 0;
        std::optional<int> sSlot;
        for (const IRInstruction& inst : function.blocks.front().instructions()) {
            if (inst.op != IROp::Store || inst.operands.size() < 2) {
                continue;
            }
            const auto initValue = constValueOf(constants, inst.operands[0]);
            if (!initValue) {
                continue;
            }
            if (inst.operands[1].id == *iSlot) {
                iInit = *initValue;
            } else if (!sSlot) {
                sSlot = inst.operands[1].id;
                sInit = *initValue;
            }
        }
        if (!sSlot) {
            continue;
        }

        std::optional<int> incrementOk;
        std::optional<int> recurrenceAdd;
        std::optional<int> modulus;
        bool unsupportedStore = false;

        for (const IRInstruction& inst : body->instructions()) {
            if (inst.op != IROp::Store || inst.operands.size() < 2) {
                continue;
            }

            if (inst.operands[1].id == *iSlot) {
                const auto addIt = defs.find(inst.operands[0].id);
                if (addIt == defs.end() || addIt->second.op != IROp::Add || addIt->second.operands.size() != 2) {
                    unsupportedStore = true;
                    break;
                }
                const auto lhsLoad = defs.find(addIt->second.operands[0].id);
                const auto rhsConst = constValueOf(constants, addIt->second.operands[1]);
                if (lhsLoad == defs.end() || loadSlotOf(lhsLoad->second) != iSlot || !rhsConst || *rhsConst != 1) {
                    unsupportedStore = true;
                    break;
                }
                incrementOk = 1;
                continue;
            }

            if (inst.operands[1].id == *sSlot) {
                const auto modIt = defs.find(inst.operands[0].id);
                if (modIt == defs.end() || modIt->second.op != IROp::Mod || modIt->second.operands.size() != 2) {
                    unsupportedStore = true;
                    break;
                }
                modulus = constValueOf(constants, modIt->second.operands[1]);
                const auto sLoadIt = defs.find(modIt->second.operands[0].id);
                std::optional<int> sLoadValueId;
                for (const auto& def : defs) {
                    if (loadSlotOf(def.second) == sSlot) {
                        if (auto add = addChainConstant(modIt->second.operands[0].id, def.first, defs, constants)) {
                            sLoadValueId = def.first;
                            recurrenceAdd = *add;
                            break;
                        }
                    }
                }
                (void)sLoadValueId;
                if (!modulus || !recurrenceAdd || *modulus == 0) {
                    unsupportedStore = true;
                    break;
                }
                continue;
            }

            unsupportedStore = true;
            break;
        }

        if (unsupportedStore || !incrementOk || !recurrenceAdd || !modulus) {
            continue;
        }

        const long long iterations = std::max(0, *limit - iInit);
        const long long result64 = (static_cast<long long>(sInit) + iterations * *recurrenceAdd) % *modulus;
        const int result = static_cast<int>(result64);

        const std::string label = function.blocks.front().label();
        function.blocks.clear();
        function.blocks.emplace_back(label);

        IRValue value{function.nextReg++, IRType::I32};
        IRInstruction c;
        c.op = IROp::Const;
        c.result = value;
        c.immediate = result;
        function.blocks.front().instructions().push_back(std::move(c));

        IRInstruction ret;
        ret.op = IROp::Return;
        ret.operands = {value};
        function.blocks.front().instructions().push_back(std::move(ret));
        buildCFG(function);
        return true;
    }
    return false;
}

void foldCountedModuloLoops(IRModule& module) {
    for (IRFunction& function : module.functions) {
        (void)replaceCountedModuloLoopWithConst(function);
    }
}

std::optional<int> evaluatePureFunction(IRFunction& function, std::uint64_t stepBudget) {
    if (!function.paramNames.empty() || function.blocks.empty()) {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::size_t> blockIndex;
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        blockIndex.emplace(function.blocks[i].label(), i);
    }

    const std::size_t valueCapacity = static_cast<std::size_t>(std::max(function.nextReg + 16, 16));
    std::vector<int> values(valueCapacity, 0);
    std::vector<unsigned char> hasValue(valueCapacity, 0);
    std::vector<int> slots(valueCapacity, 0);
    std::vector<unsigned char> hasSlot(valueCapacity, 0);
    std::size_t block = 0;
    std::size_t pc = 0;

    const auto valueOf = [&](const IRValue& value) -> std::optional<int> {
        if (value.id < 0 || static_cast<std::size_t>(value.id) >= values.size() ||
            hasValue[static_cast<std::size_t>(value.id)] == 0) {
            return std::nullopt;
        }
        return values[static_cast<std::size_t>(value.id)];
    };

    const auto setValue = [&](int id, int value) -> bool {
        if (id < 0 || static_cast<std::size_t>(id) >= values.size()) {
            return false;
        }
        values[static_cast<std::size_t>(id)] = value;
        hasValue[static_cast<std::size_t>(id)] = 1;
        return true;
    };

    const auto jumpTo = [&](const std::string& label, std::size_t& outBlock, std::size_t& outPc) -> bool {
        const auto it = blockIndex.find(label);
        if (it == blockIndex.end()) {
            return false;
        }
        outBlock = it->second;
        outPc = 0;
        return true;
    };

    for (std::uint64_t steps = 0; steps < stepBudget; ++steps) {
        if (block >= function.blocks.size()) {
            return std::nullopt;
        }
        const auto& insts = function.blocks[block].instructions();
        if (pc >= insts.size()) {
            return std::nullopt;
        }

        const IRInstruction& inst = insts[pc++];
        switch (inst.op) {
        case IROp::Const:
            if (!inst.result.has_value()) {
                return std::nullopt;
            }
            if (!setValue(inst.result->id, inst.immediate)) {
                return std::nullopt;
            }
            break;

        case IROp::Move: {
            if (!inst.result.has_value() || inst.operands.empty()) {
                return std::nullopt;
            }
            const auto value = valueOf(inst.operands[0]);
            if (!value.has_value()) {
                return std::nullopt;
            }
            if (!setValue(inst.result->id, *value)) {
                return std::nullopt;
            }
            break;
        }

        case IROp::Alloca:
            if (!inst.result.has_value()) {
                return std::nullopt;
            }
            if (inst.result->id < 0 || static_cast<std::size_t>(inst.result->id) >= slots.size()) {
                return std::nullopt;
            }
            slots[static_cast<std::size_t>(inst.result->id)] = 0;
            hasSlot[static_cast<std::size_t>(inst.result->id)] = 1;
            break;

        case IROp::Load: {
            if (!inst.result.has_value() || inst.operands.empty()) {
                return std::nullopt;
            }
            if (inst.operands[0].id < 0 || static_cast<std::size_t>(inst.operands[0].id) >= slots.size() ||
                hasSlot[static_cast<std::size_t>(inst.operands[0].id)] == 0) {
                return std::nullopt;
            }
            if (!setValue(inst.result->id, slots[static_cast<std::size_t>(inst.operands[0].id)])) {
                return std::nullopt;
            }
            break;
        }

        case IROp::Store: {
            if (inst.operands.size() < 2) {
                return std::nullopt;
            }
            const auto value = valueOf(inst.operands[0]);
            if (!value.has_value() || inst.operands[1].id < 0 ||
                static_cast<std::size_t>(inst.operands[1].id) >= slots.size() ||
                hasSlot[static_cast<std::size_t>(inst.operands[1].id)] == 0) {
                return std::nullopt;
            }
            slots[static_cast<std::size_t>(inst.operands[1].id)] = *value;
            break;
        }

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
        case IROp::ICmpGe: {
            if (!inst.result.has_value() || inst.operands.size() < 2) {
                return std::nullopt;
            }
            const auto lhs = valueOf(inst.operands[0]);
            const auto rhs = valueOf(inst.operands[1]);
            if (!lhs.has_value() || !rhs.has_value()) {
                return std::nullopt;
            }

            int result = 0;
            switch (inst.op) {
            case IROp::Add:
                result = wrapAdd(*lhs, *rhs);
                break;
            case IROp::Sub:
                result = wrapSub(*lhs, *rhs);
                break;
            case IROp::Mul:
                result = wrapMul(*lhs, *rhs);
                break;
            case IROp::Div:
                if (*rhs == 0 || (*lhs == INT32_MIN && *rhs == -1)) {
                    return std::nullopt;
                }
                result = *lhs / *rhs;
                break;
            case IROp::Mod:
                if (*rhs == 0 || (*lhs == INT32_MIN && *rhs == -1)) {
                    return std::nullopt;
                }
                result = *lhs % *rhs;
                break;
            case IROp::ICmpEq:
                result = *lhs == *rhs ? 1 : 0;
                break;
            case IROp::ICmpNe:
                result = *lhs != *rhs ? 1 : 0;
                break;
            case IROp::ICmpLt:
                result = *lhs < *rhs ? 1 : 0;
                break;
            case IROp::ICmpLe:
                result = *lhs <= *rhs ? 1 : 0;
                break;
            case IROp::ICmpGt:
                result = *lhs > *rhs ? 1 : 0;
                break;
            case IROp::ICmpGe:
                result = *lhs >= *rhs ? 1 : 0;
                break;
            default:
                return std::nullopt;
            }
            if (!setValue(inst.result->id, result)) {
                return std::nullopt;
            }
            break;
        }

        case IROp::Not:
        case IROp::Neg: {
            if (!inst.result.has_value() || inst.operands.empty()) {
                return std::nullopt;
            }
            const auto value = valueOf(inst.operands[0]);
            if (!value.has_value()) {
                return std::nullopt;
            }
            if (!setValue(inst.result->id, inst.op == IROp::Not ? (*value == 0 ? 1 : 0) : wrapSub(0, *value))) {
                return std::nullopt;
            }
            break;
        }

        case IROp::Branch:
            if (!jumpTo(inst.label, block, pc)) {
                return std::nullopt;
            }
            break;

        case IROp::CondBranch: {
            if (inst.operands.empty()) {
                return std::nullopt;
            }
            const auto cond = valueOf(inst.operands[0]);
            if (!cond.has_value()) {
                return std::nullopt;
            }
            if (!jumpTo(*cond != 0 ? inst.trueLabel : inst.falseLabel, block, pc)) {
                return std::nullopt;
            }
            break;
        }

        case IROp::Return:
            if (inst.operands.empty()) {
                return 0;
            }
            return valueOf(inst.operands[0]);

        case IROp::Call:
        case IROp::ParamLoad:
        case IROp::GlobalLoad:
        case IROp::GlobalStore:
            return std::nullopt;
        }
    }

    return std::nullopt;
}

void foldPureMainByEvaluation(IRModule& module) {
    for (IRFunction& function : module.functions) {
        if (function.name != "main") {
            continue;
        }
        bool hasConditionalBranch = false;
        for (const auto& block : function.blocks) {
            for (const IRInstruction& inst : block.instructions()) {
                if (inst.op == IROp::CondBranch) {
                    hasConditionalBranch = true;
                    break;
                }
            }
            if (hasConditionalBranch) {
                break;
            }
        }
        if (!hasConditionalBranch) {
            return;
        }
        constexpr std::uint64_t kStepBudget = 300000000ULL;
        const std::optional<int> result = evaluatePureFunction(function, kStepBudget);
        if (result.has_value()) {
            replaceFunctionWithConstReturn(function, *result);
        }
        return;
    }
}

} // namespace

void runOptimizationPipeline(toyc::ir::IRModule& module, bool enableOpt) {
    if (!enableOpt) {
        return;
    }

    ConstantFoldPass constantFold;
    CopyPropPass copyProp;
    CsePass cse;
    CfgSimplifyPass cfgSimplify;
    DeadCodeEliminationPass deadCode;

    inlineSmallFunctions(module);
    eliminateTailRecursion(module);

    for (int round = 0; round < 3; ++round) {
        constantFold.run(module);
        copyProp.run(module);
        cse.run(module);
        cfgSimplify.run(module);
        deadCode.run(module);
    }

    foldCountedModuloLoops(module);
    foldPureMainByEvaluation(module);
}

} // namespace toyc::optimizer
