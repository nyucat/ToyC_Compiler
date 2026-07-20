#include "ir/basic_block.h"
#include "ir/ir.h"
#include "optimizer/optimizer.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int expectContains(const std::string& text, const std::string& needle, const std::string& testName) {
    if (text.find(needle) == std::string::npos) {
        std::cerr << "[FAIL] " << testName << " missing: " << needle << '\n';
        return 1;
    }
    std::cout << "[PASS] " << testName << '\n';
    return 0;
}

toyc::ir::IRModule buildAddModule() {
    using namespace toyc::ir;

    IRModule module;
    IRFunction function;
    function.name = "main";
    function.returnType = toyc::ast::ValueType::Int;

    BasicBlock entry("entry");
    IRInstruction c2;
    c2.op = IROp::Const;
    c2.result = IRValue{0, IRType::I32};
    c2.immediate = 2;
    entry.addInstruction(c2);

    IRInstruction c3;
    c3.op = IROp::Const;
    c3.result = IRValue{1, IRType::I32};
    c3.immediate = 3;
    entry.addInstruction(c3);

    IRInstruction add;
    add.op = IROp::Add;
    add.result = IRValue{2, IRType::I32};
    add.operands = {IRValue{0, IRType::I32}, IRValue{1, IRType::I32}};
    entry.addInstruction(add);

    IRInstruction ret;
    ret.op = IROp::Return;
    ret.operands = {IRValue{2, IRType::I32}};
    entry.addInstruction(ret);

    function.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(function));
    return module;
}

} // namespace

int main() {
    toyc::ir::IRModule module = buildAddModule();
    toyc::optimizer::runOptimizationPipeline(module, true);

    std::ostringstream out;
    toyc::ir::dumpIRModule(module, out);

    int failures = 0;
    failures += expectContains(out.str(), "const 5", "optimizer.constant-fold");

    if (failures == 0) {
        std::cout << "All optimization tests passed.\n";
        return 0;
    }
    std::cerr << failures << " optimization test(s) failed.\n";
    return 1;
}
