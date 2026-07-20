#include "ir/ir.h"

#include "ir/basic_block.h"

#include <ostream>

namespace toyc::ir {

const char* toString(IROp op) {
    switch (op) {
    case IROp::Const:
        return "const";
    case IROp::Alloca:
        return "alloca";
    case IROp::Load:
        return "load";
    case IROp::Store:
        return "store";
    case IROp::GlobalLoad:
        return "global.load";
    case IROp::GlobalStore:
        return "global.store";
    case IROp::Add:
        return "add";
    case IROp::Sub:
        return "sub";
    case IROp::Mul:
        return "mul";
    case IROp::Div:
        return "div";
    case IROp::Mod:
        return "mod";
    case IROp::ICmpEq:
        return "icmp.eq";
    case IROp::ICmpNe:
        return "icmp.ne";
    case IROp::ICmpLt:
        return "icmp.lt";
    case IROp::ICmpLe:
        return "icmp.le";
    case IROp::ICmpGt:
        return "icmp.gt";
    case IROp::ICmpGe:
        return "icmp.ge";
    case IROp::Not:
        return "not";
    case IROp::Neg:
        return "neg";
    case IROp::Call:
        return "call";
    case IROp::ParamLoad:
        return "param.load";
    case IROp::Branch:
        return "br";
    case IROp::CondBranch:
        return "br.cond";
    case IROp::Return:
        return "ret";
    }
    return "unknown";
}

const char* toString(IRType type) {
    switch (type) {
    case IRType::I32:
        return "i32";
    case IRType::Void:
        return "void";
    case IRType::Ptr:
        return "ptr";
    }
    return "unknown";
}

namespace {

void dumpValue(std::ostream& out, const IRValue& value) {
    out << '%' << value.id;
}

void dumpInstruction(std::ostream& out, const IRInstruction& inst) {
    if (inst.result.has_value()) {
        dumpValue(out, *inst.result);
        out << " = ";
    }

    out << toString(inst.op);

    switch (inst.op) {
    case IROp::Const:
        out << ' ' << inst.immediate;
        break;
    case IROp::Alloca:
        out << ' ' << toString(inst.result->type);
        break;
    case IROp::Load:
    case IROp::Not:
    case IROp::Neg:
        if (!inst.operands.empty()) {
            out << ' ';
            dumpValue(out, inst.operands.front());
        }
        break;
    case IROp::Store:
        if (inst.operands.size() >= 2) {
            out << ' ';
            dumpValue(out, inst.operands[0]);
            out << ", ";
            dumpValue(out, inst.operands[1]);
        }
        break;
    case IROp::GlobalLoad:
        out << " @" << inst.callee;
        break;
    case IROp::GlobalStore:
        if (!inst.operands.empty()) {
            out << ' ';
            dumpValue(out, inst.operands.front());
            out << ", @" << inst.callee;
        }
        break;
    case IROp::Add:
    case IROp::Sub:
    case IROp::Mul:
    case IROp::Div:
    case IROp::Mod:
    case IROp::ICmpEq:
    case IROp::ICmpNe:
    case IROp::ICmpLt:
    case IROp::ICmpLe:
    case IROp::ICmpGt:
    case IROp::ICmpGe:
        if (inst.operands.size() >= 2) {
            out << ' ';
            dumpValue(out, inst.operands[0]);
            out << ", ";
            dumpValue(out, inst.operands[1]);
        }
        break;
    case IROp::Call:
        out << ' ' << inst.callee << '(';
        for (std::size_t i = 0; i < inst.operands.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            dumpValue(out, inst.operands[i]);
        }
        out << ')';
        break;
    case IROp::ParamLoad:
        out << ' ' << inst.immediate;
        break;
    case IROp::Branch:
        out << ' ' << inst.label;
        break;
    case IROp::CondBranch:
        if (!inst.operands.empty()) {
            out << ' ';
            dumpValue(out, inst.operands.front());
        }
        out << ", " << inst.trueLabel << ", " << inst.falseLabel;
        break;
    case IROp::Return:
        if (!inst.operands.empty()) {
            out << ' ';
            dumpValue(out, inst.operands.front());
        }
        break;
    }
}

} // namespace

void dumpIRModule(const IRModule& module, std::ostream& out) {
    for (const auto& global : module.globals) {
        out << (global.isConst ? "global.const " : "global.var ");
        out << '@' << global.name << " = " << global.initValue << '\n';
    }

    if (!module.globals.empty() && !module.functions.empty()) {
        out << '\n';
    }

    for (const auto& function : module.functions) {
        out << "func " << function.name << '(';
        for (std::size_t i = 0; i < function.paramNames.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << function.paramNames[i];
        }
        out << ") -> " << (function.returnType == ast::ValueType::Int ? "i32" : "void") << " {\n";

        for (const auto& block : function.blocks) {
            out << block.label() << ":\n";
            for (const auto& inst : block.instructions()) {
                out << "  ";
                dumpInstruction(out, inst);
                out << '\n';
            }
        }

        out << "}\n\n";
    }
}

} // namespace toyc::ir
