#include "ir/ir_builder.h"
#include "ir/ir.h"
#include "optimizer/optimizer.h"

#include "tests/ir/support/symbol_table_builder.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

int expectContains(const std::string& text, const std::string& needle, const std::string& testName) {
    if (text.find(needle) == std::string::npos) {
        std::cerr << "[FAIL] " << testName << " missing: " << needle << '\n';
        return 1;
    }
    std::cout << "[PASS] " << testName << '\n';
    return 0;
}

int testReturnConstant() {
    using namespace toyc;

    ast::SourceLocation loc;
    auto body = std::make_unique<ast::BlockAST>(loc);
    body->addStatement(
        std::make_unique<ast::ReturnStmtAST>(loc, std::make_unique<ast::NumberExprAST>(loc, 42)));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    auto mainFunc =
        std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params), std::move(body));
    testutil::SymbolTableBuilder symbols;
    symbols.bindFunction(*mainFunc);

    ast::CompUnitAST unit;
    unit.addFunction(std::move(mainFunc));

    ir::IRModule module;
    ir::IRBuilder builder(module);
    builder.buildCompUnit(unit);

    std::ostringstream out;
    ir::dumpIRModule(module, out);
    const std::string text = out.str();

    int failures = 0;
    failures += expectContains(text, "func main()", "return-constant");
    failures += expectContains(text, "const 42", "return-constant.const");
    failures += expectContains(text, "ret", "return-constant.ret");
    return failures;
}

int testShortCircuitAnd() {
    using namespace toyc;

    ast::SourceLocation loc;
    auto body = std::make_unique<ast::BlockAST>(loc);

    auto flagInit = std::make_unique<ast::VarDeclAST>(loc, "flag", std::make_unique<ast::NumberExprAST>(loc, 1));
    body->addStatement(std::make_unique<ast::DeclStmtAST>(loc, std::move(flagInit)));

    auto lhs = std::make_unique<ast::IdentifierExprAST>(loc, "flag");
    auto rhs = std::make_unique<ast::NumberExprAST>(loc, 1);
    auto logicalAnd =
        std::make_unique<ast::BinaryExprAST>(loc, "&&", std::move(lhs), std::move(rhs));

    body->addStatement(std::make_unique<ast::ReturnStmtAST>(loc, std::move(logicalAnd)));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    auto mainFunc =
        std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params), std::move(body));

    testutil::SymbolTableBuilder symbols;
    symbols.addLocalVar("flag", 0);
    symbols.bindFunction(*mainFunc);

    auto* declStmt = dynamic_cast<ast::DeclStmtAST*>(mainFunc->body().statements()[0].get());
    symbols.bindDecl(declStmt->declaration());

    auto* ret = dynamic_cast<ast::ReturnStmtAST*>(mainFunc->body().statements()[1].get());
    auto* binary = dynamic_cast<ast::BinaryExprAST*>(ret->value());
    symbols.bindIdentifier(*dynamic_cast<ast::IdentifierExprAST*>(&binary->lhs()));

    ast::CompUnitAST unit;
    unit.addFunction(std::move(mainFunc));

    ir::IRModule module;
    ir::IRBuilder builder(module);
    builder.buildCompUnit(unit);

    std::ostringstream out;
    ir::dumpIRModule(module, out);
    const std::string text = out.str();

    int failures = 0;
    failures += expectContains(text, "land.rhs", "short-circuit-and.rhs");
    failures += expectContains(text, "land.false", "short-circuit-and.false");
    failures += expectContains(text, "br.cond", "short-circuit-and.branch");
    return failures;
}

int testLogicalOrNormalization() {
    using namespace toyc;

    ast::SourceLocation loc;
    auto body = std::make_unique<ast::BlockAST>(loc);
    auto lhs = std::make_unique<ast::NumberExprAST>(loc, 0);
    auto rhs = std::make_unique<ast::NumberExprAST>(loc, 5);
    auto logicalOr =
        std::make_unique<ast::BinaryExprAST>(loc, "||", std::move(lhs), std::move(rhs));
    body->addStatement(std::make_unique<ast::ReturnStmtAST>(loc, std::move(logicalOr)));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    auto mainFunc =
        std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params), std::move(body));
    testutil::SymbolTableBuilder symbols;
    symbols.bindFunction(*mainFunc);

    ast::CompUnitAST unit;
    unit.addFunction(std::move(mainFunc));

    ir::IRModule module;
    ir::IRBuilder builder(module);
    builder.buildCompUnit(unit);

    std::ostringstream out;
    ir::dumpIRModule(module, out);
    const std::string text = out.str();

    int failures = 0;
    // C 语义：0 || 5 的结果应为 1，rhs 分支需把非零操作数规范为 0/1
    failures += expectContains(text, "lor.rhs", "logical-or-normalize.rhs-block");
    failures += expectContains(text, "icmp.ne", "logical-or-normalize.bool");
    return failures;
}

int testLogicalAndNormalization() {
    using namespace toyc;

    ast::SourceLocation loc;
    auto body = std::make_unique<ast::BlockAST>(loc);
    auto lhs = std::make_unique<ast::NumberExprAST>(loc, 1);
    auto rhs = std::make_unique<ast::NumberExprAST>(loc, 5);
    auto logicalAnd =
        std::make_unique<ast::BinaryExprAST>(loc, "&&", std::move(lhs), std::move(rhs));
    body->addStatement(std::make_unique<ast::ReturnStmtAST>(loc, std::move(logicalAnd)));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    auto mainFunc =
        std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params), std::move(body));
    testutil::SymbolTableBuilder symbols;
    symbols.bindFunction(*mainFunc);

    ast::CompUnitAST unit;
    unit.addFunction(std::move(mainFunc));

    ir::IRModule module;
    ir::IRBuilder builder(module);
    builder.buildCompUnit(unit);

    std::ostringstream out;
    ir::dumpIRModule(module, out);
    const std::string text = out.str();

    int failures = 0;
    failures += expectContains(text, "land.rhs", "logical-and-normalize.rhs-block");
    failures += expectContains(text, "icmp.ne", "logical-and-normalize.bool");
    return failures;
}

int testConstantFolding() {
    using namespace toyc;

    ast::SourceLocation loc;
    auto body = std::make_unique<ast::BlockAST>(loc);
    auto expr = std::make_unique<ast::BinaryExprAST>(
        loc, "+", std::make_unique<ast::NumberExprAST>(loc, 2), std::make_unique<ast::NumberExprAST>(loc, 3));
    body->addStatement(std::make_unique<ast::ReturnStmtAST>(loc, std::move(expr)));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    auto mainFunc =
        std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params), std::move(body));
    testutil::SymbolTableBuilder symbols;
    symbols.bindFunction(*mainFunc);

    ast::CompUnitAST unit;
    unit.addFunction(std::move(mainFunc));

    ir::IRModule module;
    ir::IRBuilder builder(module);
    builder.buildCompUnit(unit);
    optimizer::runOptimizationPipeline(module, true);

    std::ostringstream out;
    ir::dumpIRModule(module, out);
    const std::string text = out.str();

    int failures = 0;
    failures += expectContains(text, "const 5", "constant-folding");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += testReturnConstant();
    failures += testShortCircuitAnd();
    failures += testLogicalOrNormalization();
    failures += testLogicalAndNormalization();
    failures += testConstantFolding();

    if (failures == 0) {
        std::cout << "All IR tests passed.\n";
        return 0;
    }
    std::cerr << failures << " IR test(s) failed.\n";
    return 1;
}
