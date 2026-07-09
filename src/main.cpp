#include "ast/ast.h"
#include "frontend/parser.h"

#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

struct Options {
    bool optimize = false;
    bool dumpAst = false;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-opt") {
            options.optimize = true;
        } else if (arg == "--dump-ast") {
            options.dumpAst = true;
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

        std::cout << "# ToyC frontend parsed successfully\n";
        std::cout << "# code generation is not implemented yet\n";
        std::cout << "# opt=" << (options.optimize ? "on" : "off") << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
