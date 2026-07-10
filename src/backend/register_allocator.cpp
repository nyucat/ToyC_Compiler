#include "backend/register_allocator.h"

#include "backend/riscv_instruction.h"

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

    if (!frame.localVarOffsets.empty()) {
        int minLocalOffset = frame.frameSize;
        for (const auto& entry : frame.localVarOffsets) {
            minLocalOffset = std::min(minLocalOffset, entry.second);
        }
        offset = minLocalOffset;
    } else if (!frame.spillOffsets.empty()) {
        offset = frame.spillOffsets.back();
    }

    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.result.has_value() && inst.result->id >= 0) {
                if (inst.op == toyc::ir::IROp::Alloca) {
                    if (frame.localVarOffsets.count(inst.result->id)) {
                        mapping[inst.result->id] =
                            RegOrSlot::fromSlot(frame.localVarOffsets.at(inst.result->id));
                    }
                } else if (mapping.find(inst.result->id) == mapping.end()) {
                    offset -= 4;
                    mapping[inst.result->id] = RegOrSlot::fromSlot(offset);
                }
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
    static constexpr int kTempPool[] = {
        Reg::T0, Reg::T1, Reg::T2, Reg::T3, Reg::T4, Reg::T5,
    };

    RegMapping mapping;
    fallback = false;
    std::size_t spillIdx = 0;

    std::size_t savedIdx = 0;
    std::size_t tempIdx = 0;

    auto assignSpill = [&](int valueId) {
        if (spillIdx >= frame.spillOffsets.size()) {
            fallback = true;
            return;
        }
        mapping[valueId] = RegOrSlot::fromSlot(frame.spillOffsets[spillIdx++]);
    };

    auto assignValue = [&](int valueId) {
        if (mapping.find(valueId) != mapping.end()) {
            return;
        }

        if (savedIdx < sizeof(kSavedPool) / sizeof(kSavedPool[0])) {
            mapping[valueId] = RegOrSlot::fromReg(kSavedPool[savedIdx++]);
            return;
        }

        if (frame.isLeafFunction && tempIdx < sizeof(kTempPool) / sizeof(kTempPool[0])) {
            mapping[valueId] = RegOrSlot::fromReg(kTempPool[tempIdx++]);
            return;
        }

        assignSpill(valueId);
    };

    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op == toyc::ir::IROp::Alloca && inst.result.has_value() && inst.result->id >= 0) {
                assignValue(inst.result->id);
                if (fallback) {
                    return {};
                }
            }
        }
    }

    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.result.has_value() && inst.result->id >= 0 &&
                inst.op != toyc::ir::IROp::Alloca) {
                assignValue(inst.result->id);
                if (fallback) {
                    return {};
                }
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
