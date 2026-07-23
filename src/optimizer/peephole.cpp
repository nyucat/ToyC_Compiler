#include "optimizer/peephole.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

bool isPowerOfTwo(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

int log2Int(int n) {
    int result = 0;
    while (n > 1) {
        n >>= 1;
        ++result;
    }
    return result;
}

bool optimizeInstruction(IRInstruction& inst) {
    if (inst.op == IROp::Mul && inst.operands.size() == 2) {
        if (inst.operands[1].id < 0) {
            return false;
        }
    }
    
    if (inst.op == IROp::Div || inst.op == IROp::Mod) {
        if (inst.operands.size() >= 2) {
        }
    }
    
    return false;
}

bool eliminateRedundantMoves(IRFunction& function) {
    bool changed = false;
    
    for (auto& block : function.blocks) {
        auto& insts = block.instructions();
        std::vector<IRInstruction> optimized;
        optimized.reserve(insts.size());
        
        std::unordered_set<int> lastMoveSrc;
        int lastMoveDst = -1;
        
        for (auto& inst : insts) {
            if (inst.op == IROp::Move && inst.operands.size() == 1 && inst.result.has_value()) {
                if (lastMoveDst >= 0 && inst.operands[0].id == lastMoveDst) {
                    IRInstruction newMove;
                    newMove.op = IROp::Move;
                    newMove.result = inst.result;
                    IRValue value;
                    value.id = lastMoveDst;
                    value.type = inst.operands[0].type;
                    newMove.operands.push_back(value);
                    optimized.push_back(std::move(newMove));
                    changed = true;
                    continue;
                }
                
                lastMoveDst = inst.result->id;
                lastMoveSrc.clear();
                lastMoveSrc.insert(inst.operands[0].id);
            } else {
                lastMoveDst = -1;
                lastMoveSrc.clear();
            }
            
            optimized.push_back(std::move(inst));
        }
        
        if (changed) {
            insts = std::move(optimized);
        }
    }
    
    return changed;
}

bool strengthReduction(IRFunction& function) {
    bool changed = false;
    
    for (auto& block : function.blocks) {
        auto& insts = block.instructions();
        
        for (auto& inst : insts) {
            if (inst.op == IROp::Mul && inst.operands.size() == 2) {
            }
        }
    }
    
    return changed;
}

}

void runPeepholeOptimizations(toyc::ir::IRModule& module) {
    for (auto& function : module.functions) {
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 3) {
            changed = false;
            changed |= eliminateRedundantMoves(function);
            changed |= strengthReduction(function);
        }
    }
}

}
