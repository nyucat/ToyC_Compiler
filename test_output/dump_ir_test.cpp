#include "frontend/parser.h"
#include "ir/ir.h"
#include "ir/ir_builder.h"
#include "sema/semantic_analyzer.h"
#include <iostream>
#include <sstream>
int main() {
    std::string src = "int main(){ int x=1; if(x>0){ return 1; } return 0; }";
    std::istringstream in(src);
    // parse from string - use parseProgram on stdin simulation
    auto program = toyc::frontend::parseProgram(src);
    toyc::sema::SemanticAnalyzer analyzer;
    (void)analyzer.analyze(*program);
    toyc::ir::IRModule module;
    toyc::ir::IRBuilder builder(module);
    builder.buildCompUnit(*program);
    toyc::ir::dumpIRModule(module, std::cout);
}
