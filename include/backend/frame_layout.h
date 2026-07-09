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
    std::vector<int> savedRegOffsets;
    std::map<int, int> localVarOffsets;
    std::set<int> usedSavedRegs;
    bool isLeafFunction = true;
    bool hasCall = false;
};

class FrameLayout {
public:
    FrameInfo layout(const toyc::ir::IRFunction& func);
    
private:
    int countLocalVars(const toyc::ir::IRFunction& func);
    int countTempValues(const toyc::ir::IRFunction& func);
    int countMaxOutgoingStackArgs(const toyc::ir::IRFunction& func);
    bool isLeafFunction(const toyc::ir::IRFunction& func);
    std::set<int> computeSavedRegs(const toyc::ir::IRFunction& func);
    int alignTo16(int size);
};

} // namespace toyc::backend
