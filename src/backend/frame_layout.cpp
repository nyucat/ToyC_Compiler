#include "backend/frame_layout.h"

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
    int savedRegsCount = static_cast<int>(frame.usedSavedRegs.size());
    
    int size = 0;
    
    if (!frame.isLeafFunction) {
        size += 4;
    }
    
    size += savedRegsCount * 4;
    
    size += localVars * 4;
    
    frame.frameSize = alignTo16(size);
    
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
