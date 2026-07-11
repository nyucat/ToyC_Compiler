#include "optimizer/optimizer.h"

#include "optimizer/cfg_simplify.h"
#include "optimizer/constant_fold.h"
#include "optimizer/copy_prop.h"
#include "optimizer/cse.h"
#include "optimizer/dead_code.h"

#include "ir/basic_block.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

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
}

} // namespace toyc::optimizer
