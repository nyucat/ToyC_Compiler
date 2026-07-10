#pragma once

#include "backend/frame_layout.h"
#include "backend/riscv_instruction.h"
#include "backend/register_allocator.h"
#include "ir/basic_block.h"

#include <map>
#include <ostream>
#include <vector>

namespace toyc::backend {

class CodeGenerator {
public:
    explicit CodeGenerator(bool optimize = false);
    
    void generate(const toyc::ir::IRModule& module, std::ostream& out);
    
private:
    bool optimize_;
    std::string exitLabel_;
    std::map<int, int> constValues_;
    
    void generateDataSection(const toyc::ir::IRModule& module, std::ostream& out);
    void generateTextSection(const toyc::ir::IRModule& module, std::ostream& out);
    void generateFunction(const toyc::ir::IRFunction& func, std::ostream& out);
    
    void generatePrologue(
        const toyc::ir::IRFunction& func,
        const FrameInfo& frame,
        std::vector<RISCVInstruction>& insts
    );
    
    void generateEpilogue(
        const toyc::ir::IRFunction& func,
        const FrameInfo& frame,
        std::vector<RISCVInstruction>& insts
    );
    
    void generateBasicBlock(
        const toyc::ir::BasicBlock& block,
        const FrameInfo& frame,
        const RegMapping& regMap,
        std::vector<RISCVInstruction>& insts
    );
    
    std::vector<RISCVInstruction> translateInstruction(
        const toyc::ir::IRInstruction& inst,
        const FrameInfo& frame,
        const RegMapping& regMap
    );
    
    int getRegOrLoadToTmp(
        int vreg,
        const RegMapping& regMap,
        int tmpReg,
        std::vector<RISCVInstruction>& insts
    );
    
    void storeResult(
        int vreg,
        int srcReg,
        const RegMapping& regMap,
        std::vector<RISCVInstruction>& insts
    );
};

} // namespace toyc::backend
