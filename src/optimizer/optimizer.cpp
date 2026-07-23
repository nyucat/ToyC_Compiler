#include "optimizer/optimizer.h"

#include "optimizer/cfg_simplify.h"
#include "optimizer/constant_fold.h"
#include "optimizer/copy_prop.h"
#include "optimizer/cse.h"
#include "optimizer/dead_code.h"
#include "optimizer/peephole.h"

#include "ir/basic_block.h"

#include <cstddef>
#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
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

bool isLocalSlotUse(const IRInstruction& inst, int slotId) {
    if (inst.op == IROp::Load && !inst.operands.empty() && inst.operands[0].id == slotId) {
        return true;
    }
    if (inst.op == IROp::Store && inst.operands.size() >= 2 && inst.operands[1].id == slotId) {
        return true;
    }
    return false;
}

bool slotAddressEscapes(const IRFunction& function, int slotId) {
    for (const auto& block : function.blocks) {
        for (const IRInstruction& inst : block.instructions()) {
            if (isLocalSlotUse(inst, slotId)) {
                continue;
            }
            if (inst.result.has_value() && inst.result->id == slotId) {
                continue;
            }
            for (const IRValue& operand : inst.operands) {
                if (operand.id == slotId) {
                    return true;
                }
            }
        }
    }
    return false;
}

IRValue resolveValueAlias(const std::unordered_map<int, IRValue>& aliases, IRValue value) {
    std::unordered_set<int> seen;
    while (value.id >= 0 && seen.insert(value.id).second) {
        const auto it = aliases.find(value.id);
        if (it == aliases.end()) {
            break;
        }
        value = it->second;
    }
    return value;
}

bool promoteSingleStoreSlots(IRFunction& function) {
    std::unordered_set<int> slots;
    for (const auto& block : function.blocks) {
        for (const IRInstruction& inst : block.instructions()) {
            if (inst.op == IROp::Alloca && inst.result.has_value()) {
                slots.insert(inst.result->id);
            }
        }
    }

    std::unordered_map<int, IRValue> singleStoreValue;
    for (int slotId : slots) {
        if (slotAddressEscapes(function, slotId)) {
            continue;
        }

        int storeCount = 0;
        IRValue storedValue{-1, IRType::I32};
        for (const auto& block : function.blocks) {
            for (const IRInstruction& inst : block.instructions()) {
                if (inst.op == IROp::Store && inst.operands.size() >= 2 && inst.operands[1].id == slotId) {
                    ++storeCount;
                    storedValue = inst.operands[0];
                }
            }
        }
        if (storeCount == 1) {
            singleStoreValue[slotId] = storedValue;
        }
    }

    if (singleStoreValue.empty()) {
        return false;
    }

    bool changed = false;
    std::unordered_set<int> removedLoads;
    std::unordered_map<int, IRValue> aliases;

    for (auto& block : function.blocks) {
        std::vector<IRInstruction> rewritten;
        rewritten.reserve(block.instructions().size());

        for (auto& inst : block.instructions()) {
            for (IRValue& operand : inst.operands) {
                operand = resolveValueAlias(aliases, operand);
            }

            if (inst.op == IROp::Load && inst.result.has_value() && !inst.operands.empty()) {
                const auto it = singleStoreValue.find(inst.operands[0].id);
                if (it != singleStoreValue.end()) {
                    aliases[inst.result->id] = resolveValueAlias(aliases, it->second);
                    removedLoads.insert(inst.result->id);
                    changed = true;
                    continue;
                }
            }

            if (inst.op == IROp::Store && inst.operands.size() >= 2 &&
                singleStoreValue.find(inst.operands[1].id) != singleStoreValue.end()) {
                changed = true;
                continue;
            }

            if (inst.op == IROp::Alloca && inst.result.has_value() &&
                singleStoreValue.find(inst.result->id) != singleStoreValue.end()) {
                changed = true;
                continue;
            }

            rewritten.push_back(std::move(inst));
        }

        block.instructions() = std::move(rewritten);
    }

    return changed;
}

bool removeDeadLocalStores(IRFunction& function) {
    std::unordered_set<int> loadedSlots;
    std::unordered_set<int> localSlots;

    for (const auto& block : function.blocks) {
        for (const IRInstruction& inst : block.instructions()) {
            if (inst.op == IROp::Alloca && inst.result.has_value()) {
                localSlots.insert(inst.result->id);
            } else if (inst.op == IROp::Load && !inst.operands.empty()) {
                loadedSlots.insert(inst.operands[0].id);
            }
        }
    }

    bool changed = false;
    for (auto& block : function.blocks) {
        std::vector<IRInstruction> rewritten;
        rewritten.reserve(block.instructions().size());
        for (auto& inst : block.instructions()) {
            if (inst.op == IROp::Store && inst.operands.size() >= 2 &&
                localSlots.count(inst.operands[1].id) > 0 && loadedSlots.count(inst.operands[1].id) == 0) {
                changed = true;
                continue;
            }
            if (inst.op == IROp::Alloca && inst.result.has_value() &&
                loadedSlots.count(inst.result->id) == 0) {
                changed = true;
                continue;
            }
            rewritten.push_back(std::move(inst));
        }
        block.instructions() = std::move(rewritten);
    }
    return changed;
}

void runMem2RegLite(IRModule& module) {
    for (IRFunction& function : module.functions) {
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 4) {
            changed = false;
            changed |= promoteSingleStoreSlots(function);
            changed |= removeDeadLocalStores(function);
        }
        if (rounds > 1) {
            buildCFG(function);
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

bool isHoistableLoopInvariantOp(IROp op) {
    switch (op) {
    case IROp::Const:
    case IROp::Move:
    case IROp::Add:
    case IROp::Sub:
    case IROp::Mul:
    case IROp::ICmpEq:
    case IROp::ICmpNe:
    case IROp::ICmpLt:
    case IROp::ICmpLe:
    case IROp::ICmpGt:
    case IROp::ICmpGe:
    case IROp::Not:
    case IROp::Neg:
        return true;
    default:
        return false;
    }
}

bool hoistLoopInvariants(IRFunction& function) {
    buildCFG(function);
    if (function.blocks.size() < 3) {
        return false;
    }

    std::unordered_map<const BasicBlock*, std::size_t> blockIndex;
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        blockIndex.emplace(&function.blocks[i], i);
    }

    bool changed = false;
    for (std::size_t latchIdx = 0; latchIdx < function.blocks.size(); ++latchIdx) {
        BasicBlock& latch = function.blocks[latchIdx];
        const IRInstruction* term = latch.terminator();
        if (term == nullptr || term->op != IROp::Branch) {
            continue;
        }

        auto headerIt = std::find_if(function.blocks.begin(), function.blocks.end(), [&](const BasicBlock& block) {
            return block.label() == term->label;
        });
        if (headerIt == function.blocks.end()) {
            continue;
        }
        const std::size_t headerIdx = static_cast<std::size_t>(std::distance(function.blocks.begin(), headerIt));
        if (headerIdx > latchIdx) {
            continue;
        }

        std::set<std::size_t> loopBlocks;
        loopBlocks.insert(headerIdx);
        loopBlocks.insert(latchIdx);
        std::vector<BasicBlock*> worklist{&latch};
        while (!worklist.empty()) {
            BasicBlock* block = worklist.back();
            worklist.pop_back();
            for (BasicBlock* pred : block->predecessors()) {
                const auto predIt = blockIndex.find(pred);
                if (predIt == blockIndex.end()) {
                    continue;
                }
                const std::size_t predIdx = predIt->second;
                if (predIdx < headerIdx || predIdx > latchIdx) {
                    continue;
                }
                if (loopBlocks.insert(predIdx).second && predIdx != headerIdx) {
                    worklist.push_back(pred);
                }
            }
        }

        std::size_t preheaderIdx = static_cast<std::size_t>(-1);
        for (BasicBlock* pred : function.blocks[headerIdx].predecessors()) {
            const auto predIt = blockIndex.find(pred);
            if (predIt == blockIndex.end() || loopBlocks.count(predIt->second) > 0) {
                continue;
            }
            if (preheaderIdx != static_cast<std::size_t>(-1)) {
                preheaderIdx = static_cast<std::size_t>(-1);
                break;
            }
            preheaderIdx = predIt->second;
        }
        if (preheaderIdx == static_cast<std::size_t>(-1)) {
            continue;
        }

        std::unordered_set<int> loopDefined;
        for (std::size_t blockIdx : loopBlocks) {
            for (const IRInstruction& inst : function.blocks[blockIdx].instructions()) {
                if (inst.result.has_value() && inst.result->id >= 0) {
                    loopDefined.insert(inst.result->id);
                }
            }
        }

        std::vector<IRInstruction> hoisted;
        bool loopChanged = true;
        while (loopChanged) {
            loopChanged = false;
            for (std::size_t blockIdx : loopBlocks) {
                auto& insts = function.blocks[blockIdx].instructions();
                std::vector<IRInstruction> kept;
                kept.reserve(insts.size());

                for (auto& inst : insts) {
                    if (!inst.result.has_value() || !isHoistableLoopInvariantOp(inst.op)) {
                        kept.push_back(std::move(inst));
                        continue;
                    }

                    bool invariant = true;
                    for (const IRValue& operand : inst.operands) {
                        if (operand.id >= 0 && loopDefined.count(operand.id) > 0) {
                            invariant = false;
                            break;
                        }
                    }

                    if (!invariant) {
                        kept.push_back(std::move(inst));
                        continue;
                    }

                    loopDefined.erase(inst.result->id);
                    hoisted.push_back(std::move(inst));
                    loopChanged = true;
                    changed = true;
                }

                insts = std::move(kept);
            }
        }

        if (!hoisted.empty()) {
            auto& preheaderInsts = function.blocks[preheaderIdx].instructions();
            auto insertPos = preheaderInsts.end();
            if (!preheaderInsts.empty() && function.blocks[preheaderIdx].terminator() != nullptr) {
                insertPos = preheaderInsts.end() - 1;
            }
            preheaderInsts.insert(insertPos, hoisted.begin(), hoisted.end());
            buildCFG(function);
            blockIndex.clear();
            for (std::size_t i = 0; i < function.blocks.size(); ++i) {
                blockIndex.emplace(&function.blocks[i], i);
            }
        }
    }

    return changed;
}

void eliminateDeadFunctions(IRModule& module) {
    std::unordered_map<std::string, const IRFunction*> functions;
    for (const IRFunction& function : module.functions) {
        functions.emplace(function.name, &function);
    }

    if (functions.find("main") == functions.end()) {
        return;
    }

    std::unordered_set<std::string> reachable;
    std::vector<std::string> worklist{"main"};
    reachable.insert("main");

    while (!worklist.empty()) {
        const std::string name = worklist.back();
        worklist.pop_back();

        const auto functionIt = functions.find(name);
        if (functionIt == functions.end()) {
            continue;
        }

        for (const auto& block : functionIt->second->blocks) {
            for (const IRInstruction& inst : block.instructions()) {
                if (inst.op != IROp::Call) {
                    continue;
                }
                if (functions.find(inst.callee) != functions.end() &&
                    reachable.insert(inst.callee).second) {
                    worklist.push_back(inst.callee);
                }
            }
        }
    }

    module.functions.erase(
        std::remove_if(module.functions.begin(), module.functions.end(), [&](const IRFunction& function) {
            return reachable.find(function.name) == reachable.end();
        }),
        module.functions.end()
    );
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
        runMem2RegLite(module);
        for (IRFunction& function : module.functions) {
            (void)hoistLoopInvariants(function);
        }
        cse.run(module);
        cfgSimplify.run(module);
        deadCode.run(module);
    }

    runPeepholeOptimizations(module);

    eliminateDeadFunctions(module);
}

} // namespace toyc::optimizer
