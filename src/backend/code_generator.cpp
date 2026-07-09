#include "backend/code_generator.h"

#include "ir/basic_block.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace toyc::backend {

namespace {

constexpr int kImm12Min = -2048;
constexpr int kImm12Max = 2047;

[[nodiscard]] bool fitsImm12(int value) {
    return value >= kImm12Min && value <= kImm12Max;
}

void emitLoadFromSp(int rd, int offset, std::vector<RISCVInstruction>& insts) {
    if (fitsImm12(offset)) {
        insts.push_back(RISCVInstruction::makeLW(rd, offset, Reg::SP));
        return;
    }
    insts.push_back(RISCVInstruction::makeLI(Reg::T6, offset));
    insts.push_back(RISCVInstruction::makeADD(Reg::T6, Reg::SP, Reg::T6));
    insts.push_back(RISCVInstruction::makeLW(rd, 0, Reg::T6));
}

void emitStoreToSp(int rs, int offset, std::vector<RISCVInstruction>& insts) {
    if (fitsImm12(offset)) {
        insts.push_back(RISCVInstruction::makeSW(rs, offset, Reg::SP));
        return;
    }
    insts.push_back(RISCVInstruction::makeLI(Reg::T6, offset));
    insts.push_back(RISCVInstruction::makeADD(Reg::T6, Reg::SP, Reg::T6));
    insts.push_back(RISCVInstruction::makeSW(rs, 0, Reg::T6));
}

void emitSpAdjust(int delta, std::vector<RISCVInstruction>& insts) {
    if (delta == 0) {
        return;
    }
    if (fitsImm12(delta)) {
        insts.push_back(RISCVInstruction::makeADDI(Reg::SP, Reg::SP, delta));
        return;
    }
    insts.push_back(RISCVInstruction::makeLI(Reg::T6, delta));
    insts.push_back(RISCVInstruction::makeADD(Reg::SP, Reg::SP, Reg::T6));
}

std::string localBlockLabel(const std::string& label) {
    std::string out = ".L";
    for (char ch : label) {
        if (ch == '.') {
            out += '_';
        } else {
            out += ch;
        }
    }
    return out;
}

int resultDestReg(int vreg, const RegMapping& regMap, int scratch) {
    auto it = regMap.find(vreg);
    if (it != regMap.end() && it->second.isReg) {
        return it->second.regOrOffset;
    }
    return scratch;
}

} // namespace

CodeGenerator::CodeGenerator(bool optimize) : optimize_(optimize) {}

void CodeGenerator::generate(const toyc::ir::IRModule& module, std::ostream& out) {
    generateDataSection(module, out);
    generateTextSection(module, out);
}

void CodeGenerator::generateDataSection(const toyc::ir::IRModule& module, std::ostream& out) {
    if (module.globals.empty()) {
        return;
    }

    out << "    .data\n";
    for (const auto& global : module.globals) {
        out << "    .globl " << global.name << "\n";
        out << "    .align 2\n";
        out << global.name << ":\n";
        out << "    .word " << global.initValue << "\n";
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
    FrameInfo frame = frameLayout.layout(func, optimize_);
    
    RegisterAllocator regAlloc;
    RegMapping regMap = regAlloc.allocate(func, frame, optimize_);
    
    std::vector<RISCVInstruction> insts;
    exitLabel_ = localBlockLabel(func.name + "_exit");
    
    out << "    .globl " << func.name << "\n";
    insts.push_back(RISCVInstruction::makeLABEL(func.name));
    
    generatePrologue(func, frame, insts);

    toyc::ir::IRFunction funcCopy = func;
    const std::vector<toyc::ir::BasicBlock*> orderedBlocks = toyc::ir::reversePostOrder(funcCopy);
    std::unordered_map<std::string, const toyc::ir::BasicBlock*> blockByLabel;
    for (const auto& block : func.blocks) {
        blockByLabel.emplace(block.label(), &block);
    }

    std::unordered_set<std::string> emitted;
    for (const toyc::ir::BasicBlock* blockPtr : orderedBlocks) {
        const auto it = blockByLabel.find(blockPtr->label());
        if (it != blockByLabel.end()) {
            generateBasicBlock(*it->second, frame, regMap, insts);
            emitted.insert(blockPtr->label());
        }
    }
    for (const auto& block : func.blocks) {
        if (emitted.find(block.label()) == emitted.end()) {
            generateBasicBlock(block, frame, regMap, insts);
        }
    }
    
    insts.push_back(RISCVInstruction::makeLABEL(exitLabel_));
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
    emitSpAdjust(-frame.frameSize, insts);

    if (!frame.isLeafFunction) {
        emitStoreToSp(Reg::RA, frame.raOffset, insts);
    }

    int idx = 0;
    for (int reg : frame.usedSavedRegs) {
        emitStoreToSp(reg, frame.savedRegOffsets[idx], insts);
        idx++;
    }
}

void CodeGenerator::generateEpilogue(
    const toyc::ir::IRFunction& func,
    const FrameInfo& frame,
    std::vector<RISCVInstruction>& insts
) {
    int idx = static_cast<int>(frame.savedRegOffsets.size()) - 1;
    for (auto it = frame.usedSavedRegs.rbegin(); it != frame.usedSavedRegs.rend(); ++it) {
        emitLoadFromSp(*it, frame.savedRegOffsets[idx], insts);
        idx--;
    }

    if (!frame.isLeafFunction) {
        emitLoadFromSp(Reg::RA, frame.raOffset, insts);
    }

    emitSpAdjust(frame.frameSize, insts);
    
    if (func.name == "main") {
        insts.push_back(RISCVInstruction::makeLI(Reg::A7, 93));
        insts.push_back(RISCVInstruction(RISCVOp::ECALL));
    } else {
        insts.push_back(RISCVInstruction::makeRET());
    }
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
        }
        emitLoadFromSp(tmpReg, it->second.regOrOffset, insts);
        return tmpReg;
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
    if (it == regMap.end()) {
        return;
    }
    if (it->second.isReg) {
        if (srcReg != it->second.regOrOffset) {
            insts.push_back(RISCVInstruction::makeMV(it->second.regOrOffset, srcReg));
        }
    } else {
        emitStoreToSp(srcReg, it->second.regOrOffset, insts);
    }
}

void CodeGenerator::generateBasicBlock(
    const toyc::ir::BasicBlock& block,
    const FrameInfo& frame,
    const RegMapping& regMap,
    std::vector<RISCVInstruction>& insts
) {
    (void)frame;
    insts.push_back(RISCVInstruction::makeLABEL(localBlockLabel(block.label())));
    
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
                auto it = regMap.find(inst.result->id);
                if (it != regMap.end() && it->second.isReg) {
                    rd = it->second.regOrOffset;
                }
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
                auto rdIt = regMap.find(inst.result->id);
                if (rdIt != regMap.end() && rdIt->second.isReg) {
                    rd = rdIt->second.regOrOffset;
                }
                const int addrVreg = inst.operands[0].id;
                auto addrIt = regMap.find(addrVreg);
                if (addrIt != regMap.end() && addrIt->second.isReg) {
                    if (rd != addrIt->second.regOrOffset) {
                        result.push_back(RISCVInstruction::makeMV(rd, addrIt->second.regOrOffset));
                    }
                } else if (frame.localVarOffsets.count(addrVreg) > 0) {
                    emitLoadFromSp(rd, frame.localVarOffsets.at(addrVreg), result);
                } else {
                    int addr = getRegOrLoadToTmp(addrVreg, regMap, Reg::T1, result);
                    result.push_back(RISCVInstruction::makeLW(rd, 0, addr));
                }
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Store: {
            if (inst.operands.size() >= 2) {
                int val = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                const int addrVreg = inst.operands[1].id;
                auto addrIt = regMap.find(addrVreg);
                if (addrIt != regMap.end() && addrIt->second.isReg) {
                    storeResult(addrVreg, val, regMap, result);
                } else if (frame.localVarOffsets.count(addrVreg) > 0) {
                    emitStoreToSp(val, frame.localVarOffsets.at(addrVreg), result);
                } else {
                    int addr = getRegOrLoadToTmp(addrVreg, regMap, Reg::T1, result);
                    result.push_back(RISCVInstruction::makeSW(val, 0, addr));
                }
            }
            break;
        }
        
        case toyc::ir::IROp::GlobalLoad: {
            if (inst.result.has_value()) {
                result.push_back(RISCVInstruction::makeLA(Reg::T1, inst.callee));
                result.push_back(RISCVInstruction::makeLW(Reg::T0, 0, Reg::T1));
                storeResult(inst.result->id, Reg::T0, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::GlobalStore: {
            if (inst.operands.size() >= 1) {
                int val = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeLA(Reg::T1, inst.callee));
                result.push_back(RISCVInstruction::makeSW(val, 0, Reg::T1));
            }
            break;
        }
        
        case toyc::ir::IROp::Add: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeADD(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }        case toyc::ir::IROp::Sub: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeSUB(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Mul: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeMUL(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Div: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeDIV(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Mod: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeREM(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpEq: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeSUB(Reg::T2, rs1, rs2));
                result.push_back(RISCVInstruction::makeSLTIU(Reg::T2, Reg::T2, 1));
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
                result.push_back(RISCVInstruction::makeSLTIU(Reg::T1, rs, 1));
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
            const size_t argCount = inst.operands.size();

            for (size_t i = 8; i < argCount; ++i) {
                const int val = getRegOrLoadToTmp(inst.operands[i].id, regMap, Reg::T0, result);
                emitStoreToSp(val, static_cast<int>((i - 8) * 4), result);
            }

            for (size_t i = 0; i < argCount && i < 8; ++i) {
                const int val = getRegOrLoadToTmp(inst.operands[i].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeMV(static_cast<int>(Reg::A0 + i), val));
            }

            result.push_back(RISCVInstruction::makeCALL(inst.callee));

            if (inst.result.has_value()) {
                storeResult(inst.result->id, Reg::A0, regMap, result);
            }
            break;
        }

        case toyc::ir::IROp::ParamLoad: {
            if (inst.result.has_value()) {
                const int paramIdx = inst.immediate;
                if (paramIdx < 8) {
                    result.push_back(RISCVInstruction::makeMV(Reg::T0, Reg::A0 + paramIdx));
                    storeResult(inst.result->id, Reg::T0, regMap, result);
                } else {
                    const int offset = frame.frameSize + (paramIdx - 8) * 4;
                    emitLoadFromSp(Reg::T0, offset, result);
                    storeResult(inst.result->id, Reg::T0, regMap, result);
                }
            }
            break;
        }
        
        case toyc::ir::IROp::Branch: {
            result.push_back(RISCVInstruction::makeJ(localBlockLabel(inst.label)));
            break;
        }
        
        case toyc::ir::IROp::CondBranch: {
            if (!inst.operands.empty()) {
                int cond = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeBNE(cond, Reg::ZERO, localBlockLabel(inst.trueLabel)));
                result.push_back(RISCVInstruction::makeJ(localBlockLabel(inst.falseLabel)));
            }
            break;
        }
        
        case toyc::ir::IROp::Return: {
            if (!inst.operands.empty()) {
                int val = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeMV(Reg::A0, val));
            }
            result.push_back(RISCVInstruction::makeJ(exitLabel_));
            break;
        }
        
        default:
            break;
    }
    
    return result;
}

} // namespace toyc::backend
