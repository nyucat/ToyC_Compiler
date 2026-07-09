#pragma once

#include <string>
#include <vector>

namespace toyc::backend {

enum class RISCVOp {
    ADD, ADDI, SUB, MUL, DIV, REM,
    AND, ANDI, OR, ORI, XOR, XORI,
    SLL, SLLI, SRL, SRLI, SRA, SRAI,
    SLT, SLTI, SLTU, SLTIU,
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    JAL, JALR,
    LB, LH, LW, LBU, LHU,
    SB, SH, SW,
    LUI, AUIPC,
    MV, LI, LA, NEG, NOT,
    J, CALL, RET,
    ECALL, EBREAK,
    LABEL
};

struct RISCVOperand {
    enum Type { Reg, Imm, Label, Symbol };
    Type type = Reg;
    int regId = 0;
    int immValue = 0;
    std::string name;
    
    static RISCVOperand reg(int id) {
        RISCVOperand op;
        op.type = Reg;
        op.regId = id;
        return op;
    }
    
    static RISCVOperand imm(int value) {
        RISCVOperand op;
        op.type = Imm;
        op.immValue = value;
        return op;
    }
    
    static RISCVOperand label(const std::string& lbl) {
        RISCVOperand op;
        op.type = Label;
        op.name = lbl;
        return op;
    }
    
    static RISCVOperand symbol(const std::string& sym) {
        RISCVOperand op;
        op.type = Symbol;
        op.name = sym;
        return op;
    }
};

class RISCVInstruction {
public:
    RISCVOp op = RISCVOp::ADD;
    std::vector<RISCVOperand> operands;
    std::string comment;
    
    RISCVInstruction() = default;
    explicit RISCVInstruction(RISCVOp opcode) : op(opcode) {}
    
    RISCVInstruction& addReg(int regId) {
        operands.push_back(RISCVOperand::reg(regId));
        return *this;
    }
    
    RISCVInstruction& addImm(int value) {
        operands.push_back(RISCVOperand::imm(value));
        return *this;
    }
    
    RISCVInstruction& addLabel(const std::string& lbl) {
        operands.push_back(RISCVOperand::label(lbl));
        return *this;
    }
    
    RISCVInstruction& addSymbol(const std::string& sym) {
        operands.push_back(RISCVOperand::symbol(sym));
        return *this;
    }
    
    [[nodiscard]] std::string format() const;
    [[nodiscard]] bool isPseudo() const;
    [[nodiscard]] bool isControlFlow() const;
    
    static RISCVInstruction makeLABEL(const std::string& name) {
        RISCVInstruction inst(RISCVOp::LABEL);
        inst.addLabel(name);
        return inst;
    }
    
    static RISCVInstruction makeADD(int rd, int rs1, int rs2) {
        RISCVInstruction inst(RISCVOp::ADD);
        inst.addReg(rd).addReg(rs1).addReg(rs2);
        return inst;
    }
    
    static RISCVInstruction makeADDI(int rd, int rs, int imm) {
        RISCVInstruction inst(RISCVOp::ADDI);
        inst.addReg(rd).addReg(rs).addImm(imm);
        return inst;
    }
    
    static RISCVInstruction makeSUB(int rd, int rs1, int rs2) {
        RISCVInstruction inst(RISCVOp::SUB);
        inst.addReg(rd).addReg(rs1).addReg(rs2);
        return inst;
    }
    
    static RISCVInstruction makeMUL(int rd, int rs1, int rs2) {
        RISCVInstruction inst(RISCVOp::MUL);
        inst.addReg(rd).addReg(rs1).addReg(rs2);
        return inst;
    }
    
    static RISCVInstruction makeDIV(int rd, int rs1, int rs2) {
        RISCVInstruction inst(RISCVOp::DIV);
        inst.addReg(rd).addReg(rs1).addReg(rs2);
        return inst;
    }
    
    static RISCVInstruction makeREM(int rd, int rs1, int rs2) {
        RISCVInstruction inst(RISCVOp::REM);
        inst.addReg(rd).addReg(rs1).addReg(rs2);
        return inst;
    }
    
    static RISCVInstruction makeLW(int rd, int offset, int base) {
        RISCVInstruction inst(RISCVOp::LW);
        inst.addReg(rd).addImm(offset).addReg(base);
        return inst;
    }
    
    static RISCVInstruction makeSW(int rs, int offset, int base) {
        RISCVInstruction inst(RISCVOp::SW);
        inst.addReg(rs).addImm(offset).addReg(base);
        return inst;
    }
    
    static RISCVInstruction makeLI(int rd, int imm) {
        RISCVInstruction inst(RISCVOp::LI);
        inst.addReg(rd).addImm(imm);
        return inst;
    }
    
    static RISCVInstruction makeLA(int rd, const std::string& sym) {
        RISCVInstruction inst(RISCVOp::LA);
        inst.addReg(rd).addSymbol(sym);
        return inst;
    }
    
    static RISCVInstruction makeMV(int rd, int rs) {
        RISCVInstruction inst(RISCVOp::MV);
        inst.addReg(rd).addReg(rs);
        return inst;
    }
    
    static RISCVInstruction makeJ(const std::string& label) {
        RISCVInstruction inst(RISCVOp::J);
        inst.addLabel(label);
        return inst;
    }
    
    static RISCVInstruction makeCALL(const std::string& func) {
        RISCVInstruction inst(RISCVOp::CALL);
        inst.addLabel(func);
        return inst;
    }
    
    static RISCVInstruction makeRET() {
        return RISCVInstruction(RISCVOp::RET);
    }
    
    static RISCVInstruction makeBEQ(int rs1, int rs2, const std::string& label) {
        RISCVInstruction inst(RISCVOp::BEQ);
        inst.addReg(rs1).addReg(rs2).addLabel(label);
        return inst;
    }
    
    static RISCVInstruction makeBNE(int rs1, int rs2, const std::string& label) {
        RISCVInstruction inst(RISCVOp::BNE);
        inst.addReg(rs1).addReg(rs2).addLabel(label);
        return inst;
    }
    
    static RISCVInstruction makeBLT(int rs1, int rs2, const std::string& label) {
        RISCVInstruction inst(RISCVOp::BLT);
        inst.addReg(rs1).addReg(rs2).addLabel(label);
        return inst;
    }
    
    static RISCVInstruction makeBGE(int rs1, int rs2, const std::string& label) {
        RISCVInstruction inst(RISCVOp::BGE);
        inst.addReg(rs1).addReg(rs2).addLabel(label);
        return inst;
    }
    
    static RISCVInstruction makeSLT(int rd, int rs1, int rs2) {
        RISCVInstruction inst(RISCVOp::SLT);
        inst.addReg(rd).addReg(rs1).addReg(rs2);
        return inst;
    }
    
    static RISCVInstruction makeSLTI(int rd, int rs, int imm) {
        RISCVInstruction inst(RISCVOp::SLTI);
        inst.addReg(rd).addReg(rs).addImm(imm);
        return inst;
    }
    
    static RISCVInstruction makeSLTIU(int rd, int rs, int imm) {
        RISCVInstruction inst(RISCVOp::SLTIU);
        inst.addReg(rd).addReg(rs).addImm(imm);
        return inst;
    }
    
    static RISCVInstruction makeSLTU(int rd, int rs1, int rs2) {
        RISCVInstruction inst(RISCVOp::SLTU);
        inst.addReg(rd).addReg(rs1).addReg(rs2);
        return inst;
    }
    
    static RISCVInstruction makeXORI(int rd, int rs, int imm) {
        RISCVInstruction inst(RISCVOp::XORI);
        inst.addReg(rd).addReg(rs).addImm(imm);
        return inst;
    }
    
    static RISCVInstruction makeNEG(int rd, int rs) {
        RISCVInstruction inst(RISCVOp::NEG);
        inst.addReg(rd).addReg(rs);
        return inst;
    }
    
    static RISCVInstruction makeNOT(int rd, int rs) {
        RISCVInstruction inst(RISCVOp::NOT);
        inst.addReg(rd).addReg(rs);
        return inst;
    }
    
    static RISCVInstruction makeLUI(int rd, int imm) {
        RISCVInstruction inst(RISCVOp::LUI);
        inst.addReg(rd).addImm(imm);
        return inst;
    }
};

namespace Reg {
    constexpr int ZERO = 0;
    constexpr int RA = 1;
    constexpr int SP = 2;
    constexpr int GP = 3;
    constexpr int TP = 4;
    constexpr int T0 = 5, T1 = 6, T2 = 7;
    constexpr int S0 = 8, S1 = 9;
    constexpr int A0 = 10, A1 = 11, A2 = 12, A3 = 13, A4 = 14, A5 = 15, A6 = 16, A7 = 17;
    constexpr int S2 = 18, S3 = 19, S4 = 20, S5 = 21, S6 = 22, S7 = 23, S8 = 24, S9 = 25, S10 = 26, S11 = 27;
    constexpr int T3 = 28, T4 = 29, T5 = 30, T6 = 31;
    
    constexpr int FP = S0;
}

[[nodiscard]] std::string regName(int regId);

} // namespace toyc::backend
