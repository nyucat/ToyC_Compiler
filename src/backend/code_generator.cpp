#include "backend/code_generator.h"

#include <sstream>

namespace toyc::backend {

CodeGenerator::CodeGenerator(bool optimize) : optimize_(optimize) {}

void CodeGenerator::generate(const toyc::ir::IRModule& module, std::ostream& out) {
    generateDataSection(module, out);
    generateTextSection(module, out);
}

void CodeGenerator::generateDataSection(const toyc::ir::IRModule& module, std::ostream& out) {
    bool hasData = false;
    for (const auto& global : module.globals) {
        if (!global.isConst) {
            hasData = true;
            break;
        }
    }
    
    if (!hasData) return;
    
    out << "    .data\n";
    for (const auto& global : module.globals) {
        if (!global.isConst) {
            out << "    .align 2\n";
            out << global.name << ":\n";
            out << "    .word " << global.initValue << "\n";
        }
    }
    out << "\n";
}

void CodeGenerator::generateTextSection(const toyc::ir::IRModule& module, std::ostream& out) {
    out << "    .text\n";
    for (const auto& func : module.functions) {
        generateFunction(func, out);
    }
}

void CodeGenerator::generateFunction(const toyc::ir::IRFunction& func, std::ostream& out) {
    FrameLayout frameLayout;
    FrameInfo frame = frameLayout.layout(func);
    
    RegisterAllocator regAlloc;
    RegMapping regMap = regAlloc.allocate(func, frame, optimize_);
    
    std::vector<RISCVInstruction> insts;
    
    insts.push_back(RISCVInstruction::makeLABEL(func.name));
    
    generatePrologue(func, frame, insts);
    
    for (const auto& block : func.blocks) {
        generateBasicBlock(block, frame, regMap, insts);
    }
    
    generateEpilogue(func, frame, insts);
    
    for (const auto& inst : insts) {
        out << inst.format() << "\n";
    }
    out << "\n";
}

void CodeGenerator::generatePrologue(
    const toyc::ir::IRFunction& func,
    const FrameInfo& frame,
    std::vector<RISCVInstruction>& insts
) {
    (void)func;
    if (frame.frameSize > 0) {
        insts.push_back(RISCVInstruction::makeADDI(Reg::SP, Reg::SP, -frame.frameSize));
    }
    
    if (!frame.isLeafFunction) {
        insts.push_back(RISCVInstruction::makeSW(Reg::RA, frame.raOffset, Reg::SP));
    }
    
    int idx = 0;
    for (int reg : frame.usedSavedRegs) {
        insts.push_back(RISCVInstruction::makeSW(reg, frame.savedRegOffsets[idx], Reg::SP));
        idx++;
    }
}

void CodeGenerator::generateEpilogue(
    const toyc::ir::IRFunction& func,
    const FrameInfo& frame,
    std::vector<RISCVInstruction>& insts
) {
    (void)func;
    int idx = static_cast<int>(frame.savedRegOffsets.size()) - 1;
    for (auto it = frame.usedSavedRegs.rbegin(); it != frame.usedSavedRegs.rend(); ++it) {
        insts.push_back(RISCVInstruction::makeLW(*it, frame.savedRegOffsets[idx], Reg::SP));
        idx--;
    }
    
    if (!frame.isLeafFunction) {
        insts.push_back(RISCVInstruction::makeLW(Reg::RA, frame.raOffset, Reg::SP));
    }
    
    if (frame.frameSize > 0) {
        insts.push_back(RISCVInstruction::makeADDI(Reg::SP, Reg::SP, frame.frameSize));
    }
    
    insts.push_back(RISCVInstruction::makeRET());
}

int CodeGenerator::getRegOrLoadToTmp(
    int vreg,
    const RegMapping& regMap,
    int tmpReg,
    std::vector<RISCVInstruction>& insts
) {
    auto it = regMap.find(vreg);
    if (it != regMap.end()) {
        if (it->second.isReg) {
            return it->second.regOrOffset;
        } else {
            insts.push_back(RISCVInstruction::makeLW(tmpReg, it->second.regOrOffset, Reg::SP));
            return tmpReg;
        }
    }
    return tmpReg;
}

void CodeGenerator::storeResult(
    int vreg,
    int srcReg,
    const RegMapping& regMap,
    std::vector<RISCVInstruction>& insts
) {
    auto it = regMap.find(vreg);
    if (it != regMap.end() && !it->second.isReg) {
        insts.push_back(RISCVInstruction::makeSW(srcReg, it->second.regOrOffset, Reg::SP));
    }
}

void CodeGenerator::generateBasicBlock(
    const toyc::ir::BasicBlock& block,
    const FrameInfo& frame,
    const RegMapping& regMap,
    std::vector<RISCVInstruction>& insts
) {
    (void)frame;
    insts.push_back(RISCVInstruction::makeLABEL(block.label()));
    
    for (const auto& irInst : block.instructions()) {
        auto translated = translateInstruction(irInst, frame, regMap);
        for (const auto& riscvInst : translated) {
            insts.push_back(riscvInst);
        }
    }
}

std::vector<RISCVInstruction> CodeGenerator::translateInstruction(
    const toyc::ir::IRInstruction& inst,
    const FrameInfo& frame,
    const RegMapping& regMap
) {
    std::vector<RISCVInstruction> result;
    
    switch (inst.op) {
        case toyc::ir::IROp::Const: {
            if (inst.result.has_value()) {
                int rd = Reg::T0;
                result.push_back(RISCVInstruction::makeLI(rd, inst.immediate));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Alloca:
            break;
            
        case toyc::ir::IROp::Load: {
            if (inst.result.has_value() && !inst.operands.empty()) {
                int rd = Reg::T0;
                int addr = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeLW(rd, 0, addr));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Store: {
            if (inst.operands.size() >= 2) {
                int val = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int addr = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSW(val, 0, addr));
            }
            break;
        }
        
        case toyc::ir::IROp::GlobalLoad: {
            if (inst.result.has_value() && !inst.operands.empty()) {
                result.push_back(RISCVInstruction::makeLA(Reg::T1, inst.label));
                result.push_back(RISCVInstruction::makeLW(Reg::T0, 0, Reg::T1));
                storeResult(inst.result->id, Reg::T0, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::GlobalStore: {
            if (inst.operands.size() >= 1) {
                int val = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeLA(Reg::T1, inst.label));
                result.push_back(RISCVInstruction::makeSW(val, 0, Reg::T1));
            }
            break;
        }
        
        case toyc::ir::IROp::Add: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeADD(Reg::T2, rs1, rs2));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Sub: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSUB(Reg::T2, rs1, rs2));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Mul: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeMUL(Reg::T2, rs1, rs2));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Div: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeDIV(Reg::T2, rs1, rs2));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Mod: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeREM(Reg::T2, rs1, rs2));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpEq: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSUB(Reg::T2, rs1, rs2));
                result.push_back(RISCVInstruction::makeSLTI(Reg::T2, Reg::T2, 1));
                result.push_back(RISCVInstruction::makeSLTI(Reg::T3, Reg::ZERO, 0));
                result.push_back(RISCVInstruction::makeXORI(Reg::T2, Reg::T2, 1));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpNe: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSUB(Reg::T2, rs1, rs2));
                result.push_back(RISCVInstruction::makeSLTU(Reg::T2, Reg::ZERO, Reg::T2));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpLt: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSLT(Reg::T2, rs1, rs2));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpLe: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSLT(Reg::T2, rs2, rs1));
                result.push_back(RISCVInstruction::makeXORI(Reg::T2, Reg::T2, 1));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpGt: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSLT(Reg::T2, rs2, rs1));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpGe: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSLT(Reg::T2, rs1, rs2));
                result.push_back(RISCVInstruction::makeXORI(Reg::T2, Reg::T2, 1));
                storeResult(inst.result->id, Reg::T2, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Not: {
            if (inst.result.has_value() && !inst.operands.empty()) {
                int rs = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeXORI(Reg::T1, rs, 1));
                storeResult(inst.result->id, Reg::T1, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Neg: {
            if (inst.result.has_value() && !inst.operands.empty()) {
                int rs = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeNEG(Reg::T1, rs));
                storeResult(inst.result->id, Reg::T1, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Call: {
            int argReg = Reg::A0;
            for (size_t i = 0; i < inst.operands.size() && i < 8; ++i) {
                int val = getRegOrLoadToTmp(inst.operands[i].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeMV(argReg++, val));
            }
            result.push_back(RISCVInstruction::makeCALL(inst.callee));
            if (inst.result.has_value()) {
                storeResult(inst.result->id, Reg::A0, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ParamLoad: {
            if (inst.result.has_value()) {
                int paramIdx = inst.immediate;
                if (paramIdx < 8) {
                    result.push_back(RISCVInstruction::makeMV(Reg::T0, Reg::A0 + paramIdx));
                    storeResult(inst.result->id, Reg::T0, regMap, result);
                }
            }
            break;
        }
        
        case toyc::ir::IROp::Branch: {
            result.push_back(RISCVInstruction::makeJ(inst.label));
            break;
        }
        
        case toyc::ir::IROp::CondBranch: {
            if (!inst.operands.empty()) {
                int cond = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeBNE(cond, Reg::ZERO, inst.trueLabel));
                result.push_back(RISCVInstruction::makeJ(inst.falseLabel));
            }
            break;
        }
        
        case toyc::ir::IROp::Return: {
            if (!inst.operands.empty()) {
                int val = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeMV(Reg::A0, val));
            }
            break;
        }
        
        default:
            break;
    }
    
    return result;
}

} // namespace toyc::backend
