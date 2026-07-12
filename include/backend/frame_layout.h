#pragma once

#include "ir/basic_block.h"

#include <map>
#include <set>
#include <vector>

namespace toyc::backend {

struct FrameInfo {
    int frameSize = 0;
    int raOffset = 0;
    int outgoingArgBytes = 0;
    int stackSlotLimit = 0;
    std::vector<int> savedRegOffsets;
    std::vector<int> spillOffsets;
    std::map<int, int> localVarOffsets;
    std::set<int> usedSavedRegs;
    bool isLeafFunction = true;
    bool hasCall = false;
};

class FrameLayout {
public:
    FrameInfo layout(const toyc::ir::IRFunction& func, bool optimize = false);
    FrameInfo layout(
        const toyc::ir::IRFunction& func,
        bool optimize,
        const std::set<int>& usedSavedRegs
    );
    
private:
    int countLocalVars(const toyc::ir::IRFunction& func);
    int countTempValues(const toyc::ir::IRFunction& func);
    int countMaxOutgoingStackArgs(const toyc::ir::IRFunction& func);
    bool isLeafFunction(const toyc::ir::IRFunction& func);
<<<<<<< Updated upstream
    std::set<int> computeSavedRegs(int optimizedSavedRegCount);
=======
>>>>>>> Stashed changes
    int alignTo16(int size);
};

} // namespace toyc::backend
