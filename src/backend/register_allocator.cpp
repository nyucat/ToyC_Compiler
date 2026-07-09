#include "backend/register_allocator.h"

namespace toyc::backend {

RegMapping RegisterAllocator::allocateToStack(
    const toyc::ir::IRFunction& func,
    const FrameInfo& frame
) {
    RegMapping mapping;
    int offset = frame.frameSize;
    
    offset -= static_cast<int>(frame.usedSavedRegs.size() * 4);
    
    if (!frame.isLeafFunction) {
        offset -= 4;
    }
    
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.result.has_value() && inst.result->id >= 0) {
                if (inst.op == toyc::ir::IROp::Alloca) {
                    if (frame.localVarOffsets.count(inst.result->id)) {
                        mapping[inst.result->id] = RegOrSlot::fromSlot(frame.localVarOffsets.at(inst.result->id));
                    }
                } else {
                    if (mapping.find(inst.result->id) == mapping.end()) {
                        offset -= 4;
                        mapping[inst.result->id] = RegOrSlot::fromSlot(offset);
                    }
                }
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
    (void)optimize;
    return allocateToStack(func, frame);
}

} // namespace toyc::backend
