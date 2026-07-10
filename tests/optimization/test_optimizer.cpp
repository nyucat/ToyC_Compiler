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

int expectNotContains(const std::string& text, const std::string& needle, const std::string& testName) {
    if (text.find(needle) != std::string::npos) {
        std::cerr << "[FAIL] " << testName << " unexpected: " << needle << '\n';
        return 1;
    }
    std::cout << "[PASS] " << testName << '\n';
    return 0;
}

int expectCount(const std::string& text, const std::string& needle, int expected, const std::string& testName) {
    int count = 0;
    std::size_t pos = text.find(needle);
    while (pos != std::string::npos) {
        ++count;
        pos = text.find(needle, pos + needle.size());
    }
    if (count != expected) {
        std::cerr << "[FAIL] " << testName << " expected " << expected << " occurrence(s) of " << needle
                  << ", got " << count << '\n';
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

toyc::ir::IRModule buildCopyModule() {
    using namespace toyc::ir;

    IRModule module;
    IRFunction function;
    function.name = "main";
    function.returnType = toyc::ast::ValueType::Int;

    BasicBlock entry("entry");
    IRInstruction slot;
    slot.op = IROp::Alloca;
    slot.result = IRValue{0, IRType::Ptr};
    entry.addInstruction(slot);

    IRInstruction c7;
    c7.op = IROp::Const;
    c7.result = IRValue{1, IRType::I32};
    c7.immediate = 7;
    entry.addInstruction(c7);

    IRInstruction store;
    store.op = IROp::Store;
    store.operands = {IRValue{1, IRType::I32}, IRValue{0, IRType::Ptr}};
    entry.addInstruction(store);

    IRInstruction load;
    load.op = IROp::Load;
    load.result = IRValue{2, IRType::I32};
    load.operands = {IRValue{0, IRType::Ptr}};
    entry.addInstruction(load);

    IRInstruction ret;
    ret.op = IROp::Return;
    ret.operands = {IRValue{2, IRType::I32}};
    entry.addInstruction(ret);

    function.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(function));
    return module;
}

toyc::ir::IRModule buildCseModule() {
    using namespace toyc::ir;

    IRModule module;
    IRFunction function;
    function.name = "main";
    function.returnType = toyc::ast::ValueType::Int;

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

    IRInstruction add0;
    add0.op = IROp::Add;
    add0.result = IRValue{2, IRType::I32};
    add0.operands = {IRValue{0, IRType::I32}, IRValue{1, IRType::I32}};
    entry.addInstruction(add0);

    IRInstruction add1 = add0;
    add1.result = IRValue{3, IRType::I32};
    entry.addInstruction(add1);

    IRInstruction ret;
    ret.op = IROp::Return;
    ret.operands = {IRValue{3, IRType::I32}};
    entry.addInstruction(ret);

    function.paramNames = {"a", "b"};
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

    toyc::ir::IRModule copyModule = buildCopyModule();
    toyc::optimizer::runOptimizationPipeline(copyModule, true);
    std::ostringstream copyOut;
    toyc::ir::dumpIRModule(copyModule, copyOut);
    failures += expectNotContains(copyOut.str(), "load", "optimizer.copy-prop");
    failures += expectContains(copyOut.str(), "ret %1", "optimizer.copy-prop-return");

    toyc::ir::IRModule cseModule = buildCseModule();
    toyc::optimizer::runOptimizationPipeline(cseModule, true);
    std::ostringstream cseOut;
    toyc::ir::dumpIRModule(cseModule, cseOut);
    failures += expectCount(cseOut.str(), " = add ", 1, "optimizer.cse");
    failures += expectContains(cseOut.str(), "ret %2", "optimizer.cse-return");

    if (failures == 0) {
        std::cout << "All optimization tests passed.\n";
        return 0;
    }
    std::cerr << failures << " optimization test(s) failed.\n";
    return 1;
}
