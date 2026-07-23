#include "backend/code_generator.h"

#include "ir/basic_block.h"

#include <sstream>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace toyc::backend {

namespace {

constexpr int kImm12Min = -2048;
constexpr int kImm12Max = 2047;

[[nodiscard]] bool fitsImm12(int value) {
    return value >= kImm12Min && value <= kImm12Max;
}

[[nodiscard]] int powerOfTwoShift(int value) {
    if (value <= 0 || (value & (value - 1)) != 0) {
        return -1;
    }
    int shift = 0;
    while ((1 << shift) != value) {
        ++shift;
    }
    return shift;
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
    std::string out;
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

bool emitMulByConstant(int rd, int rs, int multiplier, std::vector<RISCVInstruction>& insts) {
    if (multiplier == 0) {
        insts.push_back(RISCVInstruction::makeLI(rd, 0));
        return true;
    }
    if (multiplier == 1) {
        if (rd != rs) {
            insts.push_back(RISCVInstruction::makeMV(rd, rs));
        }
        return true;
    }
    if (multiplier == -1) {
        insts.push_back(RISCVInstruction::makeNEG(rd, rs));
        return true;
    }

    const bool negative = multiplier < 0;
    const long long magnitude = negative ? -static_cast<long long>(multiplier) : multiplier;
    if (magnitude <= 0 || magnitude >= (1LL << 31)) {
        return false;
    }

    std::vector<int> bits;
    for (int shift = 0; shift < 31; ++shift) {
        if ((magnitude & (1LL << shift)) != 0) {
            bits.push_back(shift);
        }
    }

    if (bits.size() > 2) {
        const int shift = powerOfTwoShift(static_cast<int>(magnitude + 1));
        if (shift > 0) {
            int src = rs;
            if (rd == rs) {
                insts.push_back(RISCVInstruction::makeMV(Reg::T6, rs));
                src = Reg::T6;
            }
            insts.push_back(RISCVInstruction(RISCVOp::SLLI).addReg(rd).addReg(src).addImm(shift));
            insts.push_back(RISCVInstruction::makeSUB(rd, rd, src));
            if (negative) {
                insts.push_back(RISCVInstruction::makeNEG(rd, rd));
            }
            return true;
        }
        return false;
    }

    int src = rs;
    if (rd == rs && bits.size() == 2) {
        insts.push_back(RISCVInstruction::makeMV(Reg::T6, rs));
        src = Reg::T6;
    }

    if (bits.size() == 1) {
        insts.push_back(RISCVInstruction(RISCVOp::SLLI).addReg(rd).addReg(src).addImm(bits[0]));
    } else {
        if (bits[0] == 0) {
            insts.push_back(RISCVInstruction(RISCVOp::SLLI).addReg(rd).addReg(src).addImm(bits[1]));
            insts.push_back(RISCVInstruction::makeADD(rd, rd, src));
        } else {
            insts.push_back(RISCVInstruction(RISCVOp::SLLI).addReg(rd).addReg(src).addImm(bits[0]));
            insts.push_back(RISCVInstruction(RISCVOp::SLLI).addReg(Reg::T5).addReg(src).addImm(bits[1]));
            insts.push_back(RISCVInstruction::makeADD(rd, rd, Reg::T5));
        }
    }

    if (negative) {
        insts.push_back(RISCVInstruction::makeNEG(rd, rd));
    }
    return true;
}

std::set<int> usedSavedRegsFromMapping(const RegMapping& regMap) {
    std::set<int> regs;
    for (const auto& entry : regMap) {
        if (!entry.second.isReg) {
            continue;
        }
        const int reg = entry.second.regOrOffset;
        if (reg >= Reg::S2 && reg <= Reg::S11) {
            regs.insert(reg);
        }
    }
    return regs;
}

bool isTempReg(int reg) {
    return (reg >= Reg::T0 && reg <= Reg::T2) || (reg >= Reg::T3 && reg <= Reg::T6);
}

bool readsReg(const RISCVInstruction& inst, int reg) {
    switch (inst.op) {
    case RISCVOp::ADD:
    case RISCVOp::SUB:
    case RISCVOp::MUL:
    case RISCVOp::DIV:
    case RISCVOp::REM:
    case RISCVOp::AND:
    case RISCVOp::OR:
    case RISCVOp::XOR:
    case RISCVOp::SLL:
    case RISCVOp::SRL:
    case RISCVOp::SRA:
    case RISCVOp::SLT:
    case RISCVOp::SLTU:
        return inst.operands.size() >= 3 &&
               (inst.operands[1].regId == reg || inst.operands[2].regId == reg);
    case RISCVOp::ADDI:
    case RISCVOp::ANDI:
    case RISCVOp::ORI:
    case RISCVOp::XORI:
    case RISCVOp::SLLI:
    case RISCVOp::SRLI:
    case RISCVOp::SRAI:
    case RISCVOp::SLTI:
    case RISCVOp::SLTIU:
    case RISCVOp::MV:
    case RISCVOp::NEG:
    case RISCVOp::NOT:
        return inst.operands.size() >= 2 && inst.operands[1].regId == reg;
    case RISCVOp::BEQ:
    case RISCVOp::BNE:
    case RISCVOp::BLT:
    case RISCVOp::BGE:
    case RISCVOp::BLTU:
    case RISCVOp::BGEU:
        return inst.operands.size() >= 2 &&
               (inst.operands[0].regId == reg || inst.operands[1].regId == reg);
    case RISCVOp::SW:
    case RISCVOp::SB:
    case RISCVOp::SH:
        return inst.operands.size() >= 3 &&
               (inst.operands[0].regId == reg || inst.operands[2].regId == reg);
    case RISCVOp::LW:
    case RISCVOp::LB:
    case RISCVOp::LH:
    case RISCVOp::LBU:
    case RISCVOp::LHU:
        return inst.operands.size() >= 3 && inst.operands[2].regId == reg;
    case RISCVOp::JALR:
        return inst.operands.size() >= 2 && inst.operands[1].regId == reg;
    default:
        return false;
    }
}

bool writesReg(const RISCVInstruction& inst, int reg) {
    switch (inst.op) {
    case RISCVOp::ADD:
    case RISCVOp::ADDI:
    case RISCVOp::SUB:
    case RISCVOp::MUL:
    case RISCVOp::DIV:
    case RISCVOp::REM:
    case RISCVOp::AND:
    case RISCVOp::ANDI:
    case RISCVOp::OR:
    case RISCVOp::ORI:
    case RISCVOp::XOR:
    case RISCVOp::XORI:
    case RISCVOp::SLL:
    case RISCVOp::SLLI:
    case RISCVOp::SRL:
    case RISCVOp::SRLI:
    case RISCVOp::SRA:
    case RISCVOp::SRAI:
    case RISCVOp::SLT:
    case RISCVOp::SLTI:
    case RISCVOp::SLTU:
    case RISCVOp::SLTIU:
    case RISCVOp::LW:
    case RISCVOp::LB:
    case RISCVOp::LH:
    case RISCVOp::LBU:
    case RISCVOp::LHU:
    case RISCVOp::LUI:
    case RISCVOp::AUIPC:
    case RISCVOp::MV:
    case RISCVOp::LI:
    case RISCVOp::LA:
    case RISCVOp::NEG:
    case RISCVOp::NOT:
    case RISCVOp::JAL:
    case RISCVOp::JALR:
        return !inst.operands.empty() && inst.operands[0].regId == reg;
    case RISCVOp::CALL:
        return reg == Reg::A0;
    default:
        return false;
    }
}

bool regDeadBeforeNextWriteOrControl(const std::vector<RISCVInstruction>& insts, std::size_t start, int reg) {
    for (std::size_t i = start; i < insts.size(); ++i) {
        const RISCVInstruction& inst = insts[i];
        if (readsReg(inst, reg)) {
            return false;
        }
        if (writesReg(inst, reg)) {
            return true;
        }
        if (inst.op == RISCVOp::LABEL || inst.isControlFlow()) {
            return true;
        }
    }
    return true;
}

bool isSavedReg(int reg) {
    return reg >= Reg::S2 && reg <= Reg::S11;
}

std::vector<RISCVInstruction> removeUnusedSavedRegSaves(std::vector<RISCVInstruction> insts) {
    std::set<std::pair<int, int>> savedSlots;
    for (const RISCVInstruction& inst : insts) {
        if (inst.op == RISCVOp::SW && inst.operands.size() >= 3 &&
            inst.operands[0].type == RISCVOperand::Reg &&
            inst.operands[1].type == RISCVOperand::Imm &&
            inst.operands[2].type == RISCVOperand::Reg &&
            inst.operands[2].regId == Reg::SP &&
            isSavedReg(inst.operands[0].regId)) {
            savedSlots.emplace(inst.operands[0].regId, inst.operands[1].immValue);
        }
    }

    std::set<int> writtenSavedRegs;
    for (const RISCVInstruction& inst : insts) {
        if (inst.op == RISCVOp::LW && inst.operands.size() >= 3 &&
            inst.operands[0].type == RISCVOperand::Reg &&
            inst.operands[1].type == RISCVOperand::Imm &&
            inst.operands[2].type == RISCVOperand::Reg &&
            inst.operands[2].regId == Reg::SP &&
            savedSlots.find({inst.operands[0].regId, inst.operands[1].immValue}) != savedSlots.end()) {
            continue;
        }
        if (!inst.operands.empty() && inst.operands[0].type == RISCVOperand::Reg &&
            isSavedReg(inst.operands[0].regId) && writesReg(inst, inst.operands[0].regId)) {
            writtenSavedRegs.insert(inst.operands[0].regId);
        }
    }

    std::vector<RISCVInstruction> out;
    out.reserve(insts.size());
    for (auto& inst : insts) {
        if ((inst.op == RISCVOp::SW || inst.op == RISCVOp::LW) && inst.operands.size() >= 3 &&
            inst.operands[0].type == RISCVOperand::Reg &&
            inst.operands[2].type == RISCVOperand::Reg &&
            inst.operands[2].regId == Reg::SP &&
            isSavedReg(inst.operands[0].regId) &&
            writtenSavedRegs.find(inst.operands[0].regId) == writtenSavedRegs.end()) {
            continue;
        }
        out.push_back(std::move(inst));
    }
    return out;
}

std::vector<RISCVInstruction> peepholeOptimize(std::vector<RISCVInstruction> insts) {
    std::vector<RISCVInstruction> out;
    out.reserve(insts.size());

    for (std::size_t i = 0; i < insts.size(); ++i) {
        RISCVInstruction inst = std::move(insts[i]);

        if (inst.op == RISCVOp::MV && inst.operands.size() >= 2 &&
            inst.operands[0].regId == inst.operands[1].regId) {
            continue;
        }

        if (inst.op == RISCVOp::LI && inst.operands.size() >= 2 &&
            isTempReg(inst.operands[0].regId) && i + 1 < insts.size()) {
            const RISCVInstruction& next = insts[i + 1];
            if (next.op == RISCVOp::MV && next.operands.size() >= 2 &&
                next.operands[1].regId == inst.operands[0].regId &&
                regDeadBeforeNextWriteOrControl(insts, i + 2, inst.operands[0].regId)) {
                out.push_back(RISCVInstruction::makeLI(next.operands[0].regId, inst.operands[1].immValue));
                ++i;
                continue;
            }
        }

        if (inst.op == RISCVOp::MV && inst.operands.size() >= 2 &&
            isTempReg(inst.operands[0].regId) && i + 1 < insts.size()) {
            const RISCVInstruction& next = insts[i + 1];
            if (next.op == RISCVOp::MV && next.operands.size() >= 2 &&
                next.operands[1].regId == inst.operands[0].regId &&
                regDeadBeforeNextWriteOrControl(insts, i + 2, inst.operands[0].regId)) {
                if (next.operands[0].regId != inst.operands[1].regId) {
                    out.push_back(RISCVInstruction::makeMV(next.operands[0].regId, inst.operands[1].regId));
                }
                ++i;
                continue;
            }
        }

        if (inst.op == RISCVOp::CALL && i + 2 < insts.size()) {
            const RISCVInstruction& save = insts[i + 1];
            const RISCVInstruction& restore = insts[i + 2];
            if (save.op == RISCVOp::MV && restore.op == RISCVOp::MV &&
                save.operands.size() >= 2 && restore.operands.size() >= 2 &&
                save.operands[1].regId == Reg::A0 &&
                restore.operands[0].regId == Reg::A0 &&
                restore.operands[1].regId == save.operands[0].regId) {
                out.push_back(std::move(inst));
                i += 2;
                continue;
            }
        }

        out.push_back(std::move(inst));
    }

    return removeUnusedSavedRegSaves(std::move(out));
}

FrameInfo compactFrame(
    const toyc::ir::IRFunction& func,
    const FrameInfo& oldFrame,
    RegMapping& regMap,
    const std::set<int>& usedSavedRegs
) {
    FrameInfo frame;
    frame.isLeafFunction = oldFrame.isLeafFunction;
    frame.hasCall = oldFrame.hasCall;
    frame.outgoingArgBytes = oldFrame.outgoingArgBytes;
    frame.stackSlotLimit = oldFrame.stackSlotLimit;
    frame.usedSavedRegs = usedSavedRegs;

    std::set<int> stackValueIds;
    for (const auto& entry : regMap) {
        if (!entry.second.isReg) {
            stackValueIds.insert(entry.first);
        }
    }

    int size = frame.outgoingArgBytes;
    if (!frame.isLeafFunction) {
        size += 4;
    }
    size += static_cast<int>(frame.usedSavedRegs.size() * 4);
    size += static_cast<int>(stackValueIds.size() * 4);
    frame.frameSize = (size + 15) & ~15;

    int offset = frame.frameSize;
    if (!frame.isLeafFunction) {
        offset -= 4;
        frame.raOffset = offset;
    }

    for (std::size_t i = 0; i < frame.usedSavedRegs.size(); ++i) {
        offset -= 4;
        frame.savedRegOffsets.push_back(offset);
    }

    for (int valueId : stackValueIds) {
        offset -= 4;
        regMap[valueId] = RegOrSlot::fromSlot(offset);
        for (const auto& block : func.blocks) {
            for (const auto& inst : block.instructions()) {
                if (inst.op == toyc::ir::IROp::Alloca && inst.result.has_value() &&
                    inst.result->id == valueId) {
                    frame.localVarOffsets[valueId] = offset;
                }
            }
        }
        if (frame.localVarOffsets.find(valueId) == frame.localVarOffsets.end()) {
            frame.spillOffsets.push_back(offset);
        }
    }

    return frame;
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
    if (optimize_) {
        frame = frameLayout.layout(func, optimize_, usedSavedRegsFromMapping(regMap));
        regMap = regAlloc.allocate(func, frame, optimize_);
        frame = compactFrame(func, frame, regMap, usedSavedRegsFromMapping(regMap));
    }

    constValues_.clear();
    currentFunctionName_ = func.name;
    fallthroughLabel_.clear();
    nextLocalLabelId_ = 0;
    useCounts_.clear();
    for (const auto& block : func.blocks) {
        for (const auto& inst : block.instructions()) {
            if (inst.op == toyc::ir::IROp::Const && inst.result.has_value()) {
                constValues_[inst.result->id] = inst.immediate;
            }
            for (const auto& operand : inst.operands) {
                if (operand.id >= 0) {
                    useCounts_[operand.id]++;
                }
            }
        }
    }
    
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

    std::vector<const toyc::ir::BasicBlock*> emissionOrder;
    std::unordered_set<std::string> emitted;
    for (const toyc::ir::BasicBlock* blockPtr : orderedBlocks) {
        const auto it = blockByLabel.find(blockPtr->label());
        if (it != blockByLabel.end()) {
            emissionOrder.push_back(it->second);
            emitted.insert(blockPtr->label());
        }
    }
    for (const auto& block : func.blocks) {
        if (emitted.find(block.label()) == emitted.end()) {
            emissionOrder.push_back(&block);
        }
    }
    for (std::size_t i = 0; i < emissionOrder.size(); ++i) {
        const std::string nextLabel =
            i + 1 < emissionOrder.size() ? localBlockLabel(emissionOrder[i + 1]->label()) : exitLabel_;
        generateBasicBlock(*emissionOrder[i], nextLabel, frame, regMap, insts);
    }
    
    insts.push_back(RISCVInstruction::makeLABEL(exitLabel_));
    generateEpilogue(func, frame, insts);

    insts = peepholeOptimize(std::move(insts));
    
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
        const auto constIt = constValues_.find(vreg);
        if (constIt != constValues_.end()) {
            insts.push_back(RISCVInstruction::makeLI(tmpReg, constIt->second));
            return tmpReg;
        }
        emitLoadFromSp(tmpReg, it->second.regOrOffset, insts);
        return tmpReg;
    }
    const auto constIt = constValues_.find(vreg);
    if (constIt != constValues_.end()) {
        insts.push_back(RISCVInstruction::makeLI(tmpReg, constIt->second));
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
    const std::string& nextLabel,
    const FrameInfo& frame,
    const RegMapping& regMap,
    std::vector<RISCVInstruction>& insts
) {
    insts.push_back(RISCVInstruction::makeLABEL(localBlockLabel(block.label())));
    fallthroughLabel_ = nextLabel;

    const std::vector<toyc::ir::IRInstruction>& irInsts = block.instructions();
    for (std::size_t idx = 0; idx < irInsts.size(); ++idx) {
        const toyc::ir::IRInstruction& irInst = irInsts[idx];

        if (optimize_ && irInst.result.has_value() && idx + 1 < irInsts.size()) {
            const toyc::ir::IRInstruction& nextInst = irInsts[idx + 1];
            if (nextInst.op == toyc::ir::IROp::Store && nextInst.operands.size() >= 2 &&
                nextInst.operands[0].id == irInst.result->id &&
                useCounts_[irInst.result->id] == 1) {
                const auto destIt = regMap.find(nextInst.operands[1].id);
                if (destIt != regMap.end() && destIt->second.isReg) {
                    RegMapping directMap = regMap;
                    directMap[irInst.result->id] = RegOrSlot::fromReg(destIt->second.regOrOffset);
                    for (const auto& riscvInst : translateInstruction(irInst, frame, directMap)) {
                        insts.push_back(riscvInst);
                    }
                    ++idx;
                    continue;
                }
            }
        }

        if (optimize_ && idx + 1 < irInsts.size()) {
            const toyc::ir::IRInstruction& nextInst = irInsts[idx + 1];
            if (nextInst.op == toyc::ir::IROp::CondBranch && !nextInst.operands.empty() &&
                irInst.result.has_value() &&
                nextInst.operands[0].id == irInst.result->id &&
                irInst.operands.size() >= 2 &&
                (irInst.op == toyc::ir::IROp::ICmpEq ||
                 irInst.op == toyc::ir::IROp::ICmpNe ||
                 irInst.op == toyc::ir::IROp::ICmpLt ||
                 irInst.op == toyc::ir::IROp::ICmpLe ||
                 irInst.op == toyc::ir::IROp::ICmpGt ||
                 irInst.op == toyc::ir::IROp::ICmpGe)) {
                const int rs1 = getRegOrLoadToTmp(irInst.operands[0].id, regMap, Reg::T3, insts);
                const int rs2 = getRegOrLoadToTmp(irInst.operands[1].id, regMap, Reg::T4, insts);
                const std::string trueLabel = localBlockLabel(nextInst.trueLabel);
                const std::string falseLabel = localBlockLabel(nextInst.falseLabel);

                if (irInst.op == toyc::ir::IROp::ICmpLt) {
                    if (trueLabel == nextLabel) {
                        insts.push_back(RISCVInstruction::makeBGE(rs1, rs2, falseLabel));
                    } else {
                        insts.push_back(RISCVInstruction::makeBLT(rs1, rs2, trueLabel));
                        if (falseLabel != nextLabel) {
                            insts.push_back(RISCVInstruction::makeJ(falseLabel));
                        }
                    }
                    ++idx;
                    continue;
                }
                if (irInst.op == toyc::ir::IROp::ICmpGt) {
                    if (trueLabel == nextLabel) {
                        insts.push_back(RISCVInstruction::makeBGE(rs2, rs1, falseLabel));
                    } else {
                        insts.push_back(RISCVInstruction::makeBLT(rs2, rs1, trueLabel));
                        if (falseLabel != nextLabel) {
                            insts.push_back(RISCVInstruction::makeJ(falseLabel));
                        }
                    }
                    ++idx;
                    continue;
                }
                if (irInst.op == toyc::ir::IROp::ICmpEq) {
                    if (trueLabel == nextLabel) {
                        insts.push_back(RISCVInstruction::makeBNE(rs1, rs2, falseLabel));
                    } else {
                        insts.push_back(RISCVInstruction::makeBEQ(rs1, rs2, trueLabel));
                        if (falseLabel != nextLabel) {
                            insts.push_back(RISCVInstruction::makeJ(falseLabel));
                        }
                    }
                    ++idx;
                    continue;
                }
                if (irInst.op == toyc::ir::IROp::ICmpNe) {
                    if (trueLabel == nextLabel) {
                        insts.push_back(RISCVInstruction::makeBEQ(rs1, rs2, falseLabel));
                    } else {
                        insts.push_back(RISCVInstruction::makeBNE(rs1, rs2, trueLabel));
                        if (falseLabel != nextLabel) {
                            insts.push_back(RISCVInstruction::makeJ(falseLabel));
                        }
                    }
                    ++idx;
                    continue;
                }
                if (irInst.op == toyc::ir::IROp::ICmpLe) {
                    if (trueLabel == nextLabel) {
                        insts.push_back(RISCVInstruction::makeBLT(rs2, rs1, falseLabel));
                    } else {
                        insts.push_back(RISCVInstruction::makeBGE(rs2, rs1, trueLabel));
                        if (falseLabel != nextLabel) {
                            insts.push_back(RISCVInstruction::makeJ(falseLabel));
                        }
                    }
                    ++idx;
                    continue;
                }
                if (irInst.op == toyc::ir::IROp::ICmpGe) {
                    if (trueLabel == nextLabel) {
                        insts.push_back(RISCVInstruction::makeBLT(rs1, rs2, falseLabel));
                    } else {
                        insts.push_back(RISCVInstruction::makeBGE(rs1, rs2, trueLabel));
                        if (falseLabel != nextLabel) {
                            insts.push_back(RISCVInstruction::makeJ(falseLabel));
                        }
                    }
                    ++idx;
                    continue;
                }
            }
        }

        for (const auto& riscvInst : translateInstruction(irInst, frame, regMap)) {
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
                } else if (optimize_) {
                    break;
                }
                result.push_back(RISCVInstruction::makeLI(rd, inst.immediate));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }

        case toyc::ir::IROp::Move: {
            if (inst.result.has_value() && !inst.operands.empty()) {
                const int src = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                storeResult(inst.result->id, src, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Alloca:
            break;
            
        case toyc::ir::IROp::Load: {
            if (inst.result.has_value() && !inst.operands.empty()) {
                const int addrVreg = inst.operands[0].id;
                auto addrIt = regMap.find(addrVreg);
                auto rdIt = regMap.find(inst.result->id);

                if (addrIt != regMap.end() && addrIt->second.isReg && rdIt != regMap.end() &&
                    rdIt->second.isReg && rdIt->second.regOrOffset == addrIt->second.regOrOffset) {
                    break;
                }

                int rd = Reg::T0;
                if (rdIt != regMap.end() && rdIt->second.isReg) {
                    rd = rdIt->second.regOrOffset;
                }

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
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);

                const auto rhsConst = constValues_.find(inst.operands[1].id);
                if (rhsConst != constValues_.end() && fitsImm12(rhsConst->second)) {
                    if (rd == rs1) {
                        result.push_back(RISCVInstruction::makeADDI(rd, rs1, rhsConst->second));
                    } else {
                        result.push_back(RISCVInstruction::makeADDI(rd, rs1, rhsConst->second));
                    }
                    storeResult(inst.result->id, rd, regMap, result);
                    break;
                }

                const auto lhsConst = constValues_.find(inst.operands[0].id);
                if (lhsConst != constValues_.end() && fitsImm12(lhsConst->second)) {
                    int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                    result.push_back(RISCVInstruction::makeADDI(rd, rs2, lhsConst->second));
                    storeResult(inst.result->id, rd, regMap, result);
                    break;
                }

                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
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
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);

                const auto rhsConst = constValues_.find(inst.operands[1].id);
                if (rhsConst != constValues_.end()) {
                    if (emitMulByConstant(rd, rs1, rhsConst->second, result)) {
                        storeResult(inst.result->id, rd, regMap, result);
                        break;
                    }
                }

                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeMUL(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Div: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);

                const auto rhsConst = constValues_.find(inst.operands[1].id);
                if (rhsConst != constValues_.end()) {
                    const int divisor = rhsConst->second;
                    const int shift = powerOfTwoShift(divisor);
                    if (shift >= 0) {
                        if (shift == 0) {
                            storeResult(inst.result->id, rs1, regMap, result);
                        } else {
                            result.push_back(RISCVInstruction(RISCVOp::SRAI).addReg(Reg::T6).addReg(rs1).addImm(31));
                            result.push_back(RISCVInstruction::makeANDI(Reg::T6, Reg::T6, divisor - 1));
                            result.push_back(RISCVInstruction::makeADD(rd, rs1, Reg::T6));
                            result.push_back(RISCVInstruction(RISCVOp::SRAI).addReg(rd).addReg(rd).addImm(shift));
                            storeResult(inst.result->id, rd, regMap, result);
                        }
                        break;
                    }
                }

                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeDIV(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::Mod: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);

                const auto rhsConst = constValues_.find(inst.operands[1].id);
                if (rhsConst != constValues_.end()) {
                    const int divisor = rhsConst->second;
                    const int shift = powerOfTwoShift(divisor);
                    if (shift >= 0 && divisor - 1 <= kImm12Max && -divisor >= kImm12Min) {
                        result.push_back(RISCVInstruction::makeANDI(rd, rs1, divisor - 1));
                        const std::string doneLabel = localBlockLabel(
                            currentFunctionName_ + ".rem_pow2_done." + std::to_string(nextLocalLabelId_++)
                        );
                        result.push_back(RISCVInstruction::makeBGE(rs1, Reg::ZERO, doneLabel));
                        result.push_back(RISCVInstruction::makeBEQ(rd, Reg::ZERO, doneLabel));
                        result.push_back(RISCVInstruction::makeADDI(rd, rd, -divisor));
                        result.push_back(RISCVInstruction::makeLABEL(doneLabel));
                        storeResult(inst.result->id, rd, regMap, result);
                        break;
                    }
                }

                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                result.push_back(RISCVInstruction::makeREM(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpEq: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeSUB(rd, rs1, rs2));
                result.push_back(RISCVInstruction::makeSLTIU(rd, rd, 1));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpNe: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeSUB(rd, rs1, rs2));
                result.push_back(RISCVInstruction::makeSLTU(rd, Reg::ZERO, rd));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpLt: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeSLT(rd, rs1, rs2));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpLe: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeSLT(rd, rs2, rs1));
                result.push_back(RISCVInstruction::makeXORI(rd, rd, 1));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpGt: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeSLT(rd, rs2, rs1));
                storeResult(inst.result->id, rd, regMap, result);
            }
            break;
        }
        
        case toyc::ir::IROp::ICmpGe: {
            if (inst.result.has_value() && inst.operands.size() >= 2) {
                int rs1 = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                int rs2 = getRegOrLoadToTmp(inst.operands[1].id, regMap, Reg::T1, result);
                int rd = resultDestReg(inst.result->id, regMap, Reg::T2);
                result.push_back(RISCVInstruction::makeSLT(rd, rs1, rs2));
                result.push_back(RISCVInstruction::makeXORI(rd, rd, 1));
                storeResult(inst.result->id, rd, regMap, result);
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
            const std::string target = localBlockLabel(inst.label);
            if (target != fallthroughLabel_) {
                result.push_back(RISCVInstruction::makeJ(target));
            }
            break;
        }
        
        case toyc::ir::IROp::CondBranch: {
            if (!inst.operands.empty()) {
                int cond = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                const std::string trueLabel = localBlockLabel(inst.trueLabel);
                const std::string falseLabel = localBlockLabel(inst.falseLabel);
                if (trueLabel == fallthroughLabel_) {
                    result.push_back(RISCVInstruction::makeBEQ(cond, Reg::ZERO, falseLabel));
                } else {
                    result.push_back(RISCVInstruction::makeBNE(cond, Reg::ZERO, trueLabel));
                    if (falseLabel != fallthroughLabel_) {
                        result.push_back(RISCVInstruction::makeJ(falseLabel));
                    }
                }
            }
            break;
        }
        
        case toyc::ir::IROp::Return: {
            if (!inst.operands.empty()) {
                int val = getRegOrLoadToTmp(inst.operands[0].id, regMap, Reg::T0, result);
                result.push_back(RISCVInstruction::makeMV(Reg::A0, val));
            }
            if (exitLabel_ != fallthroughLabel_) {
                result.push_back(RISCVInstruction::makeJ(exitLabel_));
            }
            break;
        }
        
        default:
            break;
    }
    
    return result;
}

} // namespace toyc::backend
