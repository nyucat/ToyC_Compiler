#include "backend/frame_layout.h"

#include <algorithm>

namespace toyc::backend {

int FrameLayout::countLocalVars(const toyc::ir::IRFunction& func) {
    int count = 0;
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op == toyc::ir::IROp::Alloca) {
                count++;
            }
        }
    }
    return count;
}

int FrameLayout::countTempValues(const toyc::ir::IRFunction& func) {
    std::set<int> ids;
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.result.has_value() && inst.result->id >= 0 &&
                inst.op != toyc::ir::IROp::Alloca) {
                ids.insert(inst.result->id);
            }
        }
    }
    return static_cast<int>(ids.size());
}

int FrameLayout::countMaxOutgoingStackArgs(const toyc::ir::IRFunction& func) {
    int maxArgs = 0;
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op == toyc::ir::IROp::Call) {
                maxArgs = std::max(maxArgs, static_cast<int>(inst.operands.size()));
            }
        }
    }
    return std::max(0, maxArgs - 8);
}

bool FrameLayout::isLeafFunction(const toyc::ir::IRFunction& func) {
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op == toyc::ir::IROp::Call) {
                return false;
            }
        }
    }
    return true;
}

std::set<int> FrameLayout::computeSavedRegs(const toyc::ir::IRFunction& func) {
    std::set<int> usedRegs;
    usedRegs.insert(8);
    usedRegs.insert(9);
    return usedRegs;
}

int FrameLayout::alignTo16(int size) {
    return (size + 15) & ~15;
}

FrameInfo FrameLayout::layout(const toyc::ir::IRFunction& func) {
    FrameInfo frame;
    
    frame.isLeafFunction = isLeafFunction(func);
    frame.hasCall = !frame.isLeafFunction;
    frame.usedSavedRegs = computeSavedRegs(func);
    
    int localVars = countLocalVars(func);
    int tempValues = countTempValues(func);
    const int outgoingStackArgs = countMaxOutgoingStackArgs(func);
    int savedRegsCount = static_cast<int>(frame.usedSavedRegs.size());
    
    int size = 0;
    
    if (!frame.isLeafFunction) {
        size += 4;
    }
    
    size += savedRegsCount * 4;
    
    size += localVars * 4;
    size += tempValues * 4;
    size += outgoingStackArgs * 4;
    
    frame.frameSize = alignTo16(size);
    frame.outgoingArgBytes = outgoingStackArgs * 4;
    
    int offset = frame.frameSize;
    
    if (!frame.isLeafFunction) {
        offset -= 4;
        frame.raOffset = offset;
    }
    
    frame.savedRegOffsets.clear();
    for (int reg : frame.usedSavedRegs) {
        offset -= 4;
        frame.savedRegOffsets.push_back(offset);
    }
    
    int localVarId = 0;
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op == toyc::ir::IROp::Alloca && inst.result.has_value()) {
                offset -= 4;
                frame.localVarOffsets[inst.result->id] = offset;
            }
        }
    }
    
    return frame;
}

} // namespace toyc::backend
