#include "frontend/parser.h"
#include "ir/ir.h"
#include "ir/ir_builder.h"
#include "optimizer/optimizer.h"
#include "sema/semantic_analyzer.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int expectContains(const std::string& text, const std::string& needle, const std::string& name) {
    if (text.find(needle) != std::string::npos) {
        std::cout << "[PASS] " << name << '\n';
        return 0;
    }
    std::cout << "[FAIL] " << name << " missing: " << needle << '\n';
    return 1;
}

int runPipelineCase(const std::string& name, const std::string& source, bool optimize) {
    int failures = 0;
    try {
        auto program = toyc::frontend::parseProgram(source);

        toyc::sema::SemanticAnalyzer analyzer;
        (void)analyzer.analyze(*program);

        toyc::ir::IRModule module;
        toyc::ir::IRBuilder builder(module);
        builder.buildCompUnit(*program);
        toyc::optimizer::runOptimizationPipeline(module, optimize);

        std::ostringstream out;
        toyc::ir::dumpIRModule(module, out);
        const std::string text = out.str();

        failures += expectContains(text, "func main()", name + ".main");
        failures += expectContains(text, "ret", name + ".ret");
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] " << name << " exception: " << ex.what() << '\n';
        ++failures;
    }
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += runPipelineCase("pipeline-return-const", "int main() { return 42; }", false);
    failures += runPipelineCase("pipeline-global-const",
                                "const int N = 5;\n"
                                "int main() { return N; }",
                                false);
    failures += runPipelineCase("pipeline-opt-fold",
                                "int main() { return 2 + 3; }",
                                true);

    if (failures == 0) {
        std::cout << "All A_B_C pipeline tests passed.\n";
        return 0;
    }
    std::cerr << failures << " pipeline test(s) failed.\n";
    return 1;
}
