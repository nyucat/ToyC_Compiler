#include "backend/code_generator.h"
#include "ir/ir.h"

#include <iostream>
#include <sstream>

using namespace toyc::ir;
using namespace toyc::backend;

int main() {
    IRModule module;
    
    IRFunction mainFunc;
    mainFunc.name = "main";
    mainFunc.returnType = ast::ValueType::Int;
    mainFunc.nextReg = 0;
    mainFunc.nextBlockId = 0;
    
    BasicBlock entry("entry");
    
    IRInstruction constInst;
    constInst.op = IROp::Const;
    constInst.immediate = 42;
    IRValue result;
    result.id = mainFunc.nextReg++;
    result.type = IRType::I32;
    constInst.result = result;
    entry.addInstruction(constInst);
    
    IRInstruction retInst;
    retInst.op = IROp::Return;
    IRValue retVal;
    retVal.id = result.id;
    retVal.type = IRType::I32;
    retInst.operands.push_back(retVal);
    entry.addInstruction(retInst);
    
    mainFunc.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(mainFunc));
    
    std::ostringstream output;
    CodeGenerator codeGen(false);
    codeGen.generate(module, output);
    
    std::cout << "Generated RISC-V Assembly:\n";
    std::cout << output.str();
    
    return 0;
}
