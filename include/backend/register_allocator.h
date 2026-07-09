#pragma once

#include "backend/frame_layout.h"
#include "ir/ir.h"

#include <map>

namespace toyc::backend {

struct RegOrSlot {
    bool isReg = false;
    int regOrOffset = 0;
    
    static RegOrSlot fromReg(int regId) {
        RegOrSlot rs;
        rs.isReg = true;
        rs.regOrOffset = regId;
        return rs;
    }
    
    static RegOrSlot fromSlot(int offset) {
        RegOrSlot rs;
        rs.isReg = false;
        rs.regOrOffset = offset;
        return rs;
    }
};

using RegMapping = std::map<int, RegOrSlot>;

class RegisterAllocator {
public:
    RegMapping allocate(
        const toyc::ir::IRFunction& func,
        const FrameInfo& frame,
        bool optimize
    );
    
private:
    RegMapping allocateToStack(
        const toyc::ir::IRFunction& func,
        const FrameInfo& frame
    );

    RegMapping allocateWithRegisters(
        const toyc::ir::IRFunction& func,
        const FrameInfo& frame
    );
};

} // namespace toyc::backend
