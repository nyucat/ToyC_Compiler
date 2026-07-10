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

std::set<int> FrameLayout::computeSavedRegs(const toyc::ir::IRFunction& func, bool optimize) {
    (void)func;

    std::set<int> usedRegs;
    usedRegs.insert(8);
    usedRegs.insert(9);

    if (optimize) {
        for (int reg = 18; reg <= 27; ++reg) {
            usedRegs.insert(reg);
        }
    }

    return usedRegs;
}

int FrameLayout::alignTo16(int size) {
    return (size + 15) & ~15;
}

FrameInfo FrameLayout::layout(const toyc::ir::IRFunction& func, bool optimize) {
    FrameInfo frame;

    frame.isLeafFunction = isLeafFunction(func);
    frame.hasCall = !frame.isLeafFunction;

    const int localVars = countLocalVars(func);
    const int tempValues = countTempValues(func);
    const int localRegHeld = 0;
    const int tempRegHeld = optimize && frame.isLeafFunction
                                ? std::min(tempValues, 16 - localRegHeld)
                                : (optimize ? std::min(tempValues, 10 - localRegHeld) : 0);
    const int stackLocals = localVars - localRegHeld;
    const int stackTemps = optimize ? std::max(tempValues - tempRegHeld, 0) : tempValues;

    frame.usedSavedRegs = computeSavedRegs(func, optimize);

    const int outgoingStackArgs = countMaxOutgoingStackArgs(func);
    const int savedRegsCount = static_cast<int>(frame.usedSavedRegs.size());

    int size = 0;

    if (!frame.isLeafFunction) {
        size += 4;
    }

    size += savedRegsCount * 4;
    size += stackLocals * 4;
    size += stackTemps * 4;
    size += outgoingStackArgs * 4;

    frame.frameSize = alignTo16(size);
    frame.outgoingArgBytes = outgoingStackArgs * 4;
    frame.stackSlotLimit = frame.outgoingArgBytes;

    int offset = frame.frameSize;

    if (!frame.isLeafFunction) {
        offset -= 4;
        frame.raOffset = offset;
    }

    frame.savedRegOffsets.clear();
    for (int i = 0; i < savedRegsCount; ++i) {
        offset -= 4;
        frame.savedRegOffsets.push_back(offset);
    }

    int allocaIndex = 0;
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op != toyc::ir::IROp::Alloca || !inst.result.has_value()) {
                continue;
            }

            const bool inRegister = optimize && allocaIndex < localRegHeld;
            if (!inRegister) {
                offset -= 4;
                frame.localVarOffsets[inst.result->id] = offset;
            }
            allocaIndex++;
        }
    }

    frame.spillOffsets.clear();
    for (int i = 0; i < stackTemps; ++i) {
        offset -= 4;
        frame.spillOffsets.push_back(offset);
    }

    return frame;
}

} // namespace toyc::backend
