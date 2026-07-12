#include "backend/register_allocator.h"

#include "backend/riscv_instruction.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <vector>

namespace toyc::backend {

namespace {

struct LiveInterval {
    int valueId = -1;
    int start = 0;
    int end = 0;
    int uses = 0;
};

struct ActiveInterval {
    int valueId = -1;
    int end = 0;
    int reg = 0;
};

constexpr std::size_t savedPoolSize() {
    return 10;
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
    std::unordered_map<int, LiveInterval> intervals;

    int instIndex = 0;
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.result.has_value() && inst.result->id >= 0 &&
                inst.op != toyc::ir::IROp::Alloca &&
                inst.op != toyc::ir::IROp::Const) {
                LiveInterval interval;
                interval.valueId = inst.result->id;
                interval.start = instIndex;
                interval.end = instIndex;
                intervals[inst.result->id] = interval;
                valueIds.insert(inst.result->id);
            }
            for (const auto& operand : inst.operands) {
                if (operand.id >= 0) {
                    useCount[operand.id]++;
                    auto intervalIt = intervals.find(operand.id);
                    if (intervalIt != intervals.end()) {
                        intervalIt->second.end = std::max(intervalIt->second.end, instIndex);
                        intervalIt->second.uses++;
                    }
                }
            }
            ++instIndex;
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
            if (savedIdx < savedPoolSize()) {
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
                intervals.erase(inst.result->id);
            }
        }
    }

    std::vector<LiveInterval> orderedIntervals;
    orderedIntervals.reserve(valueIds.size());
    for (int valueId : valueIds) {
        const auto intervalIt = intervals.find(valueId);
        if (intervalIt == intervals.end() || useCount[valueId] == 0) {
            continue;
        }
        LiveInterval interval = intervalIt->second;
        interval.uses = useCount[valueId];
        orderedIntervals.push_back(interval);
    }

    std::stable_sort(orderedIntervals.begin(), orderedIntervals.end(), [](const LiveInterval& lhs, const LiveInterval& rhs) {
        if (lhs.start != rhs.start) {
            return lhs.start < rhs.start;
        }
        if (lhs.end != rhs.end) {
            return lhs.end < rhs.end;
        }
        return lhs.valueId < rhs.valueId;
    });

    std::vector<int> freeRegs;
    for (std::size_t idx = savedPoolSize(); idx > savedIdx; --idx) {
        freeRegs.push_back(kSavedPool[idx - 1]);
    }

    std::vector<ActiveInterval> active;
    const auto expireOldIntervals = [&](int start) {
        std::vector<ActiveInterval> stillActive;
        stillActive.reserve(active.size());
        for (const ActiveInterval& item : active) {
            if (item.end < start) {
                freeRegs.push_back(item.reg);
            } else {
                stillActive.push_back(item);
            }
        }
        active = std::move(stillActive);
    };

    const auto addActive = [&](const LiveInterval& interval, int reg) {
        active.push_back(ActiveInterval{interval.valueId, interval.end, reg});
        std::stable_sort(active.begin(), active.end(), [](const ActiveInterval& lhs, const ActiveInterval& rhs) {
            if (lhs.end != rhs.end) {
                return lhs.end < rhs.end;
            }
            return lhs.valueId < rhs.valueId;
        });
    };

    for (const LiveInterval& interval : orderedIntervals) {
        expireOldIntervals(interval.start);

        if (!freeRegs.empty()) {
            const int reg = freeRegs.back();
            freeRegs.pop_back();
            mapping[interval.valueId] = RegOrSlot::fromReg(reg);
            addActive(interval, reg);
            continue;
        }

        auto spillIt = std::max_element(active.begin(), active.end(), [](const ActiveInterval& lhs, const ActiveInterval& rhs) {
            if (lhs.end != rhs.end) {
                return lhs.end < rhs.end;
            }
            return lhs.valueId < rhs.valueId;
        });

        if (spillIt != active.end() && spillIt->end > interval.end) {
            const int reg = spillIt->reg;
            assignSpill(spillIt->valueId);
            if (fallback) {
                return {};
            }
            active.erase(spillIt);
            mapping[interval.valueId] = RegOrSlot::fromReg(reg);
            addActive(interval, reg);
        } else {
            assignSpill(interval.valueId);
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
