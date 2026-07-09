#include "ast/ast.h"
#include "backend/code_generator.h"
#include "frontend/parser.h"
#include "ir/ir.h"
#include "ir/ir_builder.h"
#include "optimizer/optimizer.h"
#include "sema/semantic_analyzer.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

struct Options {
    bool optimize = false;
    bool dumpAst = false;
    bool dumpIr = false;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-opt") {
            options.optimize = true;
        } else if (arg == "--dump-ast") {
            options.dumpAst = true;
        } else if (arg == "--dump-ir") {
            options.dumpIr = true;
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return options;
}

std::string readStdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        auto program = toyc::frontend::parseProgram(readStdin());

        if (options.dumpAst) {
            toyc::ast::dumpAST(*program, std::cout);
            return 0;
        }

        toyc::sema::SemanticAnalyzer analyzer;
        (void)analyzer.analyze(*program);

        toyc::ir::IRModule module;
        toyc::ir::IRBuilder builder(module);
        builder.buildCompUnit(*program);
        toyc::optimizer::runOptimizationPipeline(module, options.optimize);

        if (options.dumpIr) {
            toyc::ir::dumpIRModule(module, std::cout);
            return 0;
        }

        toyc::backend::CodeGenerator codeGen(options.optimize);
        codeGen.generate(module, std::cout);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
