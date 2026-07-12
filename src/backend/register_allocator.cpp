#include "backend/register_allocator.h"

#include "backend/riscv_instruction.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace toyc::backend {

namespace {

using BlockSet = std::unordered_set<toyc::ir::BasicBlock*>;

std::vector<toyc::ir::BasicBlock*> sortedBlocks(toyc::ir::IRFunction& func) {
    std::vector<toyc::ir::BasicBlock*> blocks;
    blocks.reserve(func.blocks.size());
    for (auto& block : func.blocks) {
        blocks.push_back(&block);
    }
    return blocks;
}

BlockSet intersectSets(const BlockSet& lhs, const BlockSet& rhs) {
    const BlockSet* smaller = &lhs;
    const BlockSet* larger = &rhs;
    if (smaller->size() > larger->size()) {
        std::swap(smaller, larger);
    }

    BlockSet result;
    for (toyc::ir::BasicBlock* block : *smaller) {
        if (larger->count(block) > 0) {
            result.insert(block);
        }
    }
    return result;
}

std::map<std::string, int> computeBlockWeights(const toyc::ir::IRFunction& func) {
    std::map<std::string, int> weights;
    for (const auto& block : func.blocks) {
        weights[block.label()] = 1;
    }
    if (func.blocks.empty()) {
        return weights;
    }

    toyc::ir::IRFunction cfgFunc = func;
    toyc::ir::buildCFG(cfgFunc);

    std::vector<toyc::ir::BasicBlock*> blocks = sortedBlocks(cfgFunc);
    BlockSet allBlocks;
    for (toyc::ir::BasicBlock* block : blocks) {
        allBlocks.insert(block);
    }

    std::unordered_map<toyc::ir::BasicBlock*, BlockSet> dominators;
    toyc::ir::BasicBlock* entry = &cfgFunc.blocks.front();
    for (toyc::ir::BasicBlock* block : blocks) {
        if (block == entry) {
            dominators[block] = BlockSet{block};
        } else {
            dominators[block] = allBlocks;
        }
    }

    bool changed = false;
    do {
        changed = false;
        for (toyc::ir::BasicBlock* block : blocks) {
            if (block == entry) {
                continue;
            }

            BlockSet next;
            bool firstPred = true;
            for (toyc::ir::BasicBlock* pred : block->predecessors()) {
                if (firstPred) {
                    next = dominators[pred];
                    firstPred = false;
                } else {
                    next = intersectSets(next, dominators[pred]);
                }
            }
            if (firstPred) {
                next.clear();
            }
            next.insert(block);

            if (next != dominators[block]) {
                dominators[block] = std::move(next);
                changed = true;
            }
        }
    } while (changed);

    std::map<std::string, int> loopDepths;
    for (toyc::ir::BasicBlock* header : blocks) {
        for (toyc::ir::BasicBlock* pred : header->predecessors()) {
            const auto domIt = dominators.find(pred);
            if (domIt == dominators.end() || domIt->second.count(header) == 0) {
                continue;
            }

            BlockSet loopBody{header, pred};
            std::deque<toyc::ir::BasicBlock*> worklist;
            worklist.push_back(pred);
            while (!worklist.empty()) {
                toyc::ir::BasicBlock* current = worklist.front();
                worklist.pop_front();
                for (toyc::ir::BasicBlock* loopPred : current->predecessors()) {
                    if (loopBody.insert(loopPred).second && loopPred != header) {
                        worklist.push_back(loopPred);
                    }
                }
            }

            for (toyc::ir::BasicBlock* block : loopBody) {
                loopDepths[block->label()]++;
            }
        }
    }

    for (const auto& [label, depth] : loopDepths) {
        int weight = 1;
        for (int i = 0; i < std::min(depth, 4); ++i) {
            weight *= 10;
        }
        weights[label] = weight;
    }

    return weights;
}

} // namespace

RegMapping RegisterAllocator::allocateToStack(
    const toyc::ir::IRFunction& func,
    const FrameInfo& frame
) {
    RegMapping mapping;
    std::size_t spillIdx = 0;

    int dynamicOffset = frame.frameSize;
    dynamicOffset -= static_cast<int>(frame.usedSavedRegs.size() * 4);
    if (!frame.isLeafFunction) {
        dynamicOffset -= 4;
    }
    if (!frame.localVarOffsets.empty()) {
        for (const auto& entry : frame.localVarOffsets) {
            dynamicOffset = std::min(dynamicOffset, entry.second);
        }
    } else if (!frame.spillOffsets.empty()) {
        dynamicOffset = frame.spillOffsets.back();
    }

    auto assignTempSlot = [&](int valueId) {
        if (spillIdx < frame.spillOffsets.size()) {
            mapping[valueId] = RegOrSlot::fromSlot(frame.spillOffsets[spillIdx++]);
            return;
        }
        dynamicOffset -= 4;
        mapping[valueId] = RegOrSlot::fromSlot(dynamicOffset);
    };

    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (!inst.result.has_value() || inst.result->id < 0) {
                continue;
            }

            if (inst.op == toyc::ir::IROp::Alloca) {
                const auto localIt = frame.localVarOffsets.find(inst.result->id);
                if (localIt != frame.localVarOffsets.end()) {
                    mapping[inst.result->id] = RegOrSlot::fromSlot(localIt->second);
                }
                continue;
            }

            if (mapping.find(inst.result->id) == mapping.end()) {
                assignTempSlot(inst.result->id);
            }
        }
    }

    return mapping;
}

RegMapping RegisterAllocator::allocateWithRegisters(
    const toyc::ir::IRFunction& func,
    const FrameInfo& frame,
    bool& fallback
) {
    static constexpr int kSavedPool[] = {
        Reg::S2, Reg::S3, Reg::S4, Reg::S5, Reg::S6,
        Reg::S7, Reg::S8, Reg::S9, Reg::S10, Reg::S11,
    };

    RegMapping mapping;
    fallback = false;
    std::size_t spillIdx = 0;

    auto assignSpill = [&](int valueId) {
        if (spillIdx >= frame.spillOffsets.size()) {
            fallback = true;
            return;
        }
        mapping[valueId] = RegOrSlot::fromSlot(frame.spillOffsets[spillIdx++]);
    };

    std::set<int> valueIds;
    std::unordered_map<int, int> useCount;
    const std::map<std::string, int> blockWeights = computeBlockWeights(func);

    for (const auto& block : func.blocks) {
        const auto weightIt = blockWeights.find(block.label());
        const int blockWeight = weightIt == blockWeights.end() ? 1 : weightIt->second;
        for (const auto& inst : block.instructions()) {
            if (inst.result.has_value() && inst.result->id >= 0 &&
                inst.op != toyc::ir::IROp::Alloca &&
                inst.op != toyc::ir::IROp::Const) {
                valueIds.insert(inst.result->id);
            }
            for (const auto& operand : inst.operands) {
                if (operand.id >= 0) {
                    useCount[operand.id] += blockWeight;
                }
            }
        }
    }

    std::size_t savedIdx = 0;
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op != toyc::ir::IROp::Alloca || !inst.result.has_value()) {
                continue;
            }
            if (frame.localVarOffsets.find(inst.result->id) != frame.localVarOffsets.end()) {
                continue;
            }
            if (savedIdx < sizeof(kSavedPool) / sizeof(kSavedPool[0])) {
                mapping[inst.result->id] = RegOrSlot::fromReg(kSavedPool[savedIdx++]);
            } else {
                fallback = true;
                return {};
            }
        }
    }

    for (const auto& entry : frame.localVarOffsets) {
        mapping[entry.first] = RegOrSlot::fromSlot(entry.second);
    }

    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op != toyc::ir::IROp::Load || !inst.result.has_value() ||
                inst.operands.empty()) {
                continue;
            }
            const auto addrIt = mapping.find(inst.operands[0].id);
            if (addrIt != mapping.end() && addrIt->second.isReg) {
                valueIds.erase(inst.result->id);
            }
        }
    }

    std::vector<int> orderedValues(valueIds.begin(), valueIds.end());
    std::stable_sort(orderedValues.begin(), orderedValues.end(), [&](int lhs, int rhs) {
        const int lhsUses = useCount[lhs];
        const int rhsUses = useCount[rhs];
        if (lhsUses != rhsUses) {
            return lhsUses > rhsUses;
        }
        return lhs < rhs;
    });

    for (int valueId : orderedValues) {
        if (savedIdx < sizeof(kSavedPool) / sizeof(kSavedPool[0])) {
            mapping[valueId] = RegOrSlot::fromReg(kSavedPool[savedIdx++]);
        } else {
            assignSpill(valueId);
            if (fallback) {
                return {};
            }
        }
    }

    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op != toyc::ir::IROp::Load || !inst.result.has_value() ||
                inst.operands.empty()) {
                continue;
            }
            const auto addrIt = mapping.find(inst.operands[0].id);
            if (addrIt != mapping.end() && addrIt->second.isReg) {
                mapping[inst.result->id] = addrIt->second;
            }
        }
    }

    return mapping;
}

RegMapping RegisterAllocator::allocate(
    const toyc::ir::IRFunction& func,
    const FrameInfo& frame,
    bool optimize
) {
    if (!optimize) {
        return allocateToStack(func, frame);
    }

    bool fallback = false;
    RegMapping regMap = allocateWithRegisters(func, frame, fallback);
    if (fallback) {
        return allocateToStack(func, frame);
    }
    return regMap;
}

} // namespace toyc::backend
