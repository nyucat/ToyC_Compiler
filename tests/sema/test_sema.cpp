#include "sema/constant_evaluator.h"
#include "sema/scope.h"
#include "sema/semantic_analyzer.h"
#include "sema/semantic_error.h"
#include "sema/symbol.h"

#include <iostream>
#include <memory>

namespace {

int failures = 0;

void expectTrue(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        ++failures;
    }
}

void testScopeShadowing() {
    auto global = std::make_unique<toyc::sema::Scope>();
    toyc::ast::Symbol globalSym;
    globalSym.name = "x";
    globalSym.kind = toyc::ast::SymbolKind::GlobalVar;
    expectTrue(global->declare(&globalSym), "scope.global.declare");

    auto inner = std::make_unique<toyc::sema::Scope>(global.get());
    toyc::ast::Symbol localSym;
    localSym.name = "x";
    localSym.kind = toyc::ast::SymbolKind::LocalVar;
    expectTrue(inner->declare(&localSym), "scope.inner.declare");
    expectTrue(inner->lookup("x") == &localSym, "scope.inner.shadow");
    expectTrue(global->lookup("x") == &globalSym, "scope.global.lookup");
}

void testConstantEvaluator() {
    toyc::sema::Scope scope;
    toyc::ast::Symbol constant;
    constant.name = "a";
    constant.kind = toyc::ast::SymbolKind::GlobalConst;
    constant.hasConstValue = true;
    constant.constValue = 5;
    scope.declare(&constant);

    toyc::sema::ConstantEvaluator evaluator(&scope);
    toyc::ast::SourceLocation loc;

    auto rhs = std::make_unique<toyc::ast::IdentifierExprAST>(loc, "a");
    rhs->resolvedSymbol = &constant;
    toyc::ast::BinaryExprAST expr(loc, "+", std::make_unique<toyc::ast::NumberExprAST>(loc, 1),
                                  std::move(rhs));

    const toyc::sema::ConstEvalResult result = evaluator.evaluate(expr);
    expectTrue(result.success && result.value == 6, "const-eval.add-const");
}

void testSemanticAnalyzerMain() {
    using namespace toyc;

    ast::SourceLocation loc;
    ast::CompUnitAST unit;

    auto body = std::make_unique<ast::BlockAST>(loc);
    body->addStatement(
        std::make_unique<ast::ReturnStmtAST>(loc, std::make_unique<ast::NumberExprAST>(loc, 7)));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    unit.addFunction(std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params),
                                                       std::move(body)));

    sema::SemanticAnalyzer analyzer;
    const sema::SemanticResult result = analyzer.analyze(unit);
    expectTrue(result.success, "sema.main.success");
    expectTrue(result.mainFunction != nullptr, "sema.main.symbol");
}

void testSemanticGlobalConst() {
    using namespace toyc;

    ast::SourceLocation loc;
    ast::CompUnitAST unit;

    unit.addDeclaration(std::make_unique<ast::ConstDeclAST>(
        loc, "limit", std::make_unique<ast::NumberExprAST>(loc, 10)));

    auto body = std::make_unique<ast::BlockAST>(loc);
    body->addStatement(
        std::make_unique<ast::ReturnStmtAST>(loc, std::make_unique<ast::NumberExprAST>(loc, 0)));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    unit.addFunction(std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params),
                                                       std::move(body)));

    sema::SemanticAnalyzer analyzer;
    const sema::SemanticResult result = analyzer.analyze(unit);
    expectTrue(result.success, "sema.global-const.success");
    expectTrue(result.globalObjects.size() == 1, "sema.global-const.count");
    expectTrue(result.globalObjects[0].isConst && result.globalObjects[0].initialValue == 10,
               "sema.global-const.value");
}

void testBreakOutsideLoopFails() {
    using namespace toyc;

    ast::SourceLocation loc;
    ast::CompUnitAST unit;

    auto body = std::make_unique<ast::BlockAST>(loc);
    body->addStatement(std::make_unique<ast::BreakStmtAST>(loc));

    std::vector<std::unique_ptr<ast::ParamAST>> params;
    unit.addFunction(std::make_unique<ast::FuncDefAST>(loc, ast::ValueType::Int, "main", std::move(params),
                                                       std::move(body)));

    sema::SemanticAnalyzer analyzer;
    bool threw = false;
    try {
        (void)analyzer.analyze(unit);
    } catch (const sema::SemanticError&) {
        threw = true;
    }
    expectTrue(threw, "sema.break-outside-loop");
}

} // namespace

int main() {
    testScopeShadowing();
    testConstantEvaluator();
    testSemanticAnalyzerMain();
    testSemanticGlobalConst();
    testBreakOutsideLoopFails();

    if (failures == 0) {
        std::cout << "All sema tests passed.\n";
        return 0;
    }
    std::cerr << failures << " sema test(s) failed.\n";
    return 1;
}
