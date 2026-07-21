#include "ast/ast.h"
#include "frontend/parser.h"
#include "sema/semantic_analyzer.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int expectTrue(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return 0;
    }
    std::cout << "[FAIL] " << name << '\n';
    return 1;
}

int runCase(const std::string& name, const std::string& source) {
    int failures = 0;
    try {
        auto program = toyc::frontend::parseProgram(source);
        toyc::sema::SemanticAnalyzer analyzer;
        const toyc::sema::SemanticResult result = analyzer.analyze(*program);
        failures += expectTrue(result.success, name + ".success");
        failures += expectTrue(result.mainFunction != nullptr, name + ".main");
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] " << name << " exception: " << ex.what() << '\n';
        ++failures;
    }
    return failures;
}

} // namespace

int main() {
    int failures = 0;

    failures += runCase("parse-return-const", "int main() { return 42; }");

    failures += runCase("parse-global-const",
                        "const int N = 5;\n"
                        "int main() { return N; }");

    failures += runCase("parse-local-shadow",
                        "int main() {\n"
                        "  int x = 1;\n"
                        "  {\n"
                        "    int x = 2;\n"
                        "    return x;\n"
                        "  }\n"
                        "}");

    failures += runCase("parse-while-break",
                        "int main() {\n"
                        "  int i = 0;\n"
                        "  while (i < 3) {\n"
                        "    i = i + 1;\n"
                        "    if (i == 2) { break; }\n"
                        "  }\n"
                        "  return i;\n"
                        "}");

    if (failures == 0) {
        std::cout << "All sema integration tests passed.\n";
        return 0;
    }
    std::cerr << failures << " sema integration test(s) failed.\n";
    return 1;
}
