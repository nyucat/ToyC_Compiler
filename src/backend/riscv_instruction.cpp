#include "backend/riscv_instruction.h"

#include <sstream>

namespace toyc::backend {

std::string regName(int regId) {
    switch (regId) {
        case Reg::ZERO: return "zero";
        case Reg::RA: return "ra";
        case Reg::SP: return "sp";
        case Reg::GP: return "gp";
        case Reg::TP: return "tp";
        case Reg::T0: return "t0";
        case Reg::T1: return "t1";
        case Reg::T2: return "t2";
        case Reg::S0: return "s0";
        case Reg::S1: return "s1";
        case Reg::A0: return "a0";
        case Reg::A1: return "a1";
        case Reg::A2: return "a2";
        case Reg::A3: return "a3";
        case Reg::A4: return "a4";
        case Reg::A5: return "a5";
        case Reg::A6: return "a6";
        case Reg::A7: return "a7";
        case Reg::S2: return "s2";
        case Reg::S3: return "s3";
        case Reg::S4: return "s4";
        case Reg::S5: return "s5";
        case Reg::S6: return "s6";
        case Reg::S7: return "s7";
        case Reg::S8: return "s8";
        case Reg::S9: return "s9";
        case Reg::S10: return "s10";
        case Reg::S11: return "s11";
        case Reg::T3: return "t3";
        case Reg::T4: return "t4";
        case Reg::T5: return "t5";
        case Reg::T6: return "t6";
        default: return "x" + std::to_string(regId);
    }
}

static std::string opToString(RISCVOp op) {
    switch (op) {
        case RISCVOp::ADD: return "add";
        case RISCVOp::ADDI: return "addi";
        case RISCVOp::SUB: return "sub";
        case RISCVOp::MUL: return "mul";
        case RISCVOp::DIV: return "div";
        case RISCVOp::REM: return "rem";
        case RISCVOp::AND: return "and";
        case RISCVOp::ANDI: return "andi";
        case RISCVOp::OR: return "or";
        case RISCVOp::ORI: return "ori";
        case RISCVOp::XOR: return "xor";
        case RISCVOp::XORI: return "xori";
        case RISCVOp::SLL: return "sll";
        case RISCVOp::SLLI: return "slli";
        case RISCVOp::SRL: return "srl";
        case RISCVOp::SRLI: return "srli";
        case RISCVOp::SRA: return "sra";
        case RISCVOp::SRAI: return "srai";
        case RISCVOp::SLT: return "slt";
        case RISCVOp::SLTI: return "slti";
        case RISCVOp::SLTU: return "sltu";
        case RISCVOp::SLTIU: return "sltiu";
        case RISCVOp::BEQ: return "beq";
        case RISCVOp::BNE: return "bne";
        case RISCVOp::BLT: return "blt";
        case RISCVOp::BGE: return "bge";
        case RISCVOp::BLTU: return "bltu";
        case RISCVOp::BGEU: return "bgeu";
        case RISCVOp::JAL: return "jal";
        case RISCVOp::JALR: return "jalr";
        case RISCVOp::LB: return "lb";
        case RISCVOp::LH: return "lh";
        case RISCVOp::LW: return "lw";
        case RISCVOp::LBU: return "lbu";
        case RISCVOp::LHU: return "lhu";
        case RISCVOp::SB: return "sb";
        case RISCVOp::SH: return "sh";
        case RISCVOp::SW: return "sw";
        case RISCVOp::LUI: return "lui";
        case RISCVOp::AUIPC: return "auipc";
        case RISCVOp::MV: return "mv";
        case RISCVOp::LI: return "li";
        case RISCVOp::LA: return "la";
        case RISCVOp::NEG: return "neg";
        case RISCVOp::NOT: return "not";
        case RISCVOp::J: return "j";
        case RISCVOp::CALL: return "call";
        case RISCVOp::RET: return "ret";
        case RISCVOp::ECALL: return "ecall";
        case RISCVOp::EBREAK: return "ebreak";
        case RISCVOp::LABEL: return "";
        default: return "unknown";
    }
}

std::string RISCVInstruction::format() const {
    std::ostringstream oss;
    
    if (op == RISCVOp::LABEL) {
        if (!operands.empty() && operands[0].type == RISCVOperand::Label) {
            oss << operands[0].name << ":";
        }
        if (!comment.empty()) {
            oss << "  # " << comment;
        }
        return oss.str();
    }
    
    oss << "    " << opToString(op);
    
    switch (op) {
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
            if (operands.size() >= 3) {
                oss << " " << regName(operands[0].regId) << ", "
                    << regName(operands[1].regId) << ", "
                    << regName(operands[2].regId);
            }
            break;
            
        case RISCVOp::ADDI:
        case RISCVOp::ANDI:
        case RISCVOp::ORI:
        case RISCVOp::XORI:
        case RISCVOp::SLLI:
        case RISCVOp::SRLI:
        case RISCVOp::SRAI:
        case RISCVOp::SLTI:
        case RISCVOp::SLTIU:
            if (operands.size() >= 3) {
                oss << " " << regName(operands[0].regId) << ", "
                    << regName(operands[1].regId) << ", "
                    << operands[2].immValue;
            }
            break;
            
        case RISCVOp::LW:
        case RISCVOp::LB:
        case RISCVOp::LH:
        case RISCVOp::LBU:
        case RISCVOp::LHU:
            if (operands.size() >= 3) {
                oss << " " << regName(operands[0].regId) << ", "
                    << operands[1].immValue << "("
                    << regName(operands[2].regId) << ")";
            }
            break;
            
        case RISCVOp::SW:
        case RISCVOp::SB:
        case RISCVOp::SH:
            if (operands.size() >= 3) {
                oss << " " << regName(operands[0].regId) << ", "
                    << operands[1].immValue << "("
                    << regName(operands[2].regId) << ")";
            }
            break;
            
        case RISCVOp::BEQ:
        case RISCVOp::BNE:
        case RISCVOp::BLT:
        case RISCVOp::BGE:
        case RISCVOp::BLTU:
        case RISCVOp::BGEU:
            if (operands.size() >= 3) {
                oss << " " << regName(operands[0].regId) << ", "
                    << regName(operands[1].regId) << ", "
                    << operands[2].name;
            }
            break;
            
        case RISCVOp::LI:
        case RISCVOp::LUI:
            if (operands.size() >= 2) {
                oss << " " << regName(operands[0].regId) << ", "
                    << operands[1].immValue;
            }
            break;
            
        case RISCVOp::LA:
            if (operands.size() >= 2) {
                oss << " " << regName(operands[0].regId) << ", "
                    << operands[1].name;
            }
            break;
            
        case RISCVOp::MV:
        case RISCVOp::NEG:
        case RISCVOp::NOT:
            if (operands.size() >= 2) {
                oss << " " << regName(operands[0].regId) << ", "
                    << regName(operands[1].regId);
            }
            break;
            
        case RISCVOp::J:
            if (!operands.empty()) {
                oss << " " << operands[0].name;
            }
            break;
            
        case RISCVOp::CALL:
            if (!operands.empty()) {
                oss << " " << operands[0].name;
            }
            break;
            
        case RISCVOp::RET:
            break;
            
        case RISCVOp::JAL:
            if (operands.size() >= 2) {
                oss << " " << regName(operands[0].regId) << ", "
                    << operands[1].name;
            }
            break;
            
        case RISCVOp::JALR:
            if (operands.size() >= 3) {
                oss << " " << regName(operands[0].regId) << ", "
                    << operands[1].immValue << "("
                    << regName(operands[2].regId) << ")";
            }
            break;
            
        default:
            break;
    }
    
    if (!comment.empty()) {
        oss << "  # " << comment;
    }
    
    return oss.str();
}

bool RISCVInstruction::isPseudo() const {
    switch (op) {
        case RISCVOp::MV:
        case RISCVOp::LI:
        case RISCVOp::LA:
        case RISCVOp::NEG:
        case RISCVOp::NOT:
        case RISCVOp::J:
        case RISCVOp::CALL:
        case RISCVOp::RET:
            return true;
        default:
            return false;
    }
}

bool RISCVInstruction::isControlFlow() const {
    switch (op) {
        case RISCVOp::BEQ:
        case RISCVOp::BNE:
        case RISCVOp::BLT:
        case RISCVOp::BGE:
        case RISCVOp::BLTU:
        case RISCVOp::BGEU:
        case RISCVOp::JAL:
        case RISCVOp::JALR:
        case RISCVOp::J:
        case RISCVOp::CALL:
        case RISCVOp::RET:
            return true;
        default:
            return false;
    }
}

} // namespace toyc::backend
