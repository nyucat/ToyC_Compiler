#include "ir/basic_block.h"
#include "ir/ir.h"
#include "optimizer/cse.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int countNeedle(const std::string& text, const std::string& needle) {
    int count = 0;
    std::size_t pos = text.find(needle);
    while (pos != std::string::npos) {
        ++count;
        pos = text.find(needle, pos + needle.size());
    }
    return count;
}

// Build IR for: %2 = sub %0, %1; %3 = sub %1, %0; ret (%2 + %3)
// Correct CSE must keep both Sub instructions (operands are reversed).
toyc::ir::IRModule buildReversedSubModule() {
    using namespace toyc::ir;

    IRModule module;
    IRFunction function;
    function.name = "main";
    function.returnType = toyc::ast::ValueType::Int;
    function.paramNames = {"a", "b"};

    BasicBlock entry("entry");

    IRInstruction param0;
    param0.op = IROp::ParamLoad;
    param0.result = IRValue{0, IRType::I32};
    param0.immediate = 0;
    entry.addInstruction(param0);

    IRInstruction param1;
    param1.op = IROp::ParamLoad;
    param1.result = IRValue{1, IRType::I32};
    param1.immediate = 1;
    entry.addInstruction(param1);

    IRInstruction subAb;
    subAb.op = IROp::Sub;
    subAb.result = IRValue{2, IRType::I32};
    subAb.operands = {IRValue{0, IRType::I32}, IRValue{1, IRType::I32}};
    entry.addInstruction(subAb);

    IRInstruction subBa;
    subBa.op = IROp::Sub;
    subBa.result = IRValue{3, IRType::I32};
    subBa.operands = {IRValue{1, IRType::I32}, IRValue{0, IRType::I32}};
    entry.addInstruction(subBa);

    IRInstruction add;
    add.op = IROp::Add;
    add.result = IRValue{4, IRType::I32};
    add.operands = {IRValue{2, IRType::I32}, IRValue{3, IRType::I32}};
    entry.addInstruction(add);

    IRInstruction ret;
    ret.op = IROp::Return;
    ret.operands = {IRValue{4, IRType::I32}};
    entry.addInstruction(ret);

    function.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(function));
    return module;
}

} // namespace

int main() {
    toyc::ir::IRModule module = buildReversedSubModule();
    toyc::optimizer::CsePass cse;
    cse.run(module);

    std::ostringstream out;
    toyc::ir::dumpIRModule(module, out);
    const std::string ir = out.str();

    const int subCount = countNeedle(ir, " = sub ");
    if (subCount != 2) {
        std::cerr << "[FAIL] cse.noncommutative-sub expected 2 sub instructions, got " << subCount
                  << "\nIR:\n"
                  << ir;
        return 1;
    }

    std::cout << "[PASS] cse.noncommutative-sub\n";
    std::cout << "All cse noncommutative tests passed.\n";
    return 0;
}
