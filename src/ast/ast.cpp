#include "ast/ast.h"

#include <string>

namespace toyc::ast {

NumberExprAST::NumberExprAST(SourceLocation location, int value)
    : ExprAST(location), value_(value) {}

IdentifierExprAST::IdentifierExprAST(SourceLocation location, std::string name)
    : ExprAST(location), name_(std::move(name)) {}

UnaryExprAST::UnaryExprAST(SourceLocation location, std::string op, std::unique_ptr<ExprAST> operand)
    : ExprAST(location), op_(std::move(op)), operand_(std::move(operand)) {}

BinaryExprAST::BinaryExprAST(SourceLocation location, std::string op, std::unique_ptr<ExprAST> lhs,
                             std::unique_ptr<ExprAST> rhs)
    : ExprAST(location), op_(std::move(op)), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

CallExprAST::CallExprAST(SourceLocation location, std::string callee,
                         std::vector<std::unique_ptr<ExprAST>> arguments)
    : ExprAST(location), callee_(std::move(callee)), arguments_(std::move(arguments)) {}

DeclAST::DeclAST(SourceLocation location, std::string name, std::unique_ptr<ExprAST> initializer)
    : ASTNode(location), name_(std::move(name)), initializer_(std::move(initializer)) {}

DeclStmtAST::DeclStmtAST(SourceLocation location, std::unique_ptr<DeclAST> declaration)
    : StmtAST(location), declaration_(std::move(declaration)) {}

AssignStmtAST::AssignStmtAST(SourceLocation location, std::string name, std::unique_ptr<ExprAST> value)
    : StmtAST(location), name_(std::move(name)), value_(std::move(value)) {}

ExprStmtAST::ExprStmtAST(SourceLocation location, std::unique_ptr<ExprAST> expression)
    : StmtAST(location), expression_(std::move(expression)) {}

BlockAST::BlockAST(SourceLocation location) : StmtAST(location) {}

void BlockAST::addStatement(std::unique_ptr<StmtAST> statement) {
    statements_.push_back(std::move(statement));
}

IfStmtAST::IfStmtAST(SourceLocation location, std::unique_ptr<ExprAST> condition,
                     std::unique_ptr<StmtAST> thenBranch, std::unique_ptr<StmtAST> elseBranch)
    : StmtAST(location),
      condition_(std::move(condition)),
      thenBranch_(std::move(thenBranch)),
      elseBranch_(std::move(elseBranch)) {}

WhileStmtAST::WhileStmtAST(SourceLocation location, std::unique_ptr<ExprAST> condition,
                           std::unique_ptr<StmtAST> body)
    : StmtAST(location), condition_(std::move(condition)), body_(std::move(body)) {}

ReturnStmtAST::ReturnStmtAST(SourceLocation location, std::unique_ptr<ExprAST> value)
    : StmtAST(location), value_(std::move(value)) {}

ParamAST::ParamAST(SourceLocation location, std::string name)
    : ASTNode(location), name_(std::move(name)) {}

FuncDefAST::FuncDefAST(SourceLocation location, ValueType returnType, std::string name,
                       std::vector<std::unique_ptr<ParamAST>> parameters, std::unique_ptr<BlockAST> body)
    : ASTNode(location),
      returnType_(returnType),
      name_(std::move(name)),
      parameters_(std::move(parameters)),
      body_(std::move(body)) {}

CompUnitAST::CompUnitAST() : ASTNode({1, 1}) {}

void CompUnitAST::addDeclaration(std::unique_ptr<DeclAST> declaration) {
    declarations_.push_back(std::move(declaration));
}

void CompUnitAST::addFunction(std::unique_ptr<FuncDefAST> function) {
    functions_.push_back(std::move(function));
}

namespace {

void indent(std::ostream& out, int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "  ";
    }
}

void dumpExpr(const ExprAST& expr, std::ostream& out, int depth) {
    indent(out, depth);
    if (const auto* number = dynamic_cast<const NumberExprAST*>(&expr)) {
        out << "NumberExpr " << number->value() << '\n';
    } else if (const auto* ident = dynamic_cast<const IdentifierExprAST*>(&expr)) {
        out << "IdentifierExpr " << ident->name() << '\n';
    } else if (const auto* unary = dynamic_cast<const UnaryExprAST*>(&expr)) {
        out << "UnaryExpr " << unary->op() << '\n';
        dumpExpr(unary->operand(), out, depth + 1);
    } else if (const auto* binary = dynamic_cast<const BinaryExprAST*>(&expr)) {
        out << "BinaryExpr " << binary->op() << '\n';
        dumpExpr(binary->lhs(), out, depth + 1);
        dumpExpr(binary->rhs(), out, depth + 1);
    } else if (const auto* call = dynamic_cast<const CallExprAST*>(&expr)) {
        out << "CallExpr " << call->callee() << '\n';
        for (const auto& argument : call->arguments()) {
            dumpExpr(*argument, out, depth + 1);
        }
    }
}

void dumpDecl(const DeclAST& decl, std::ostream& out, int depth) {
    indent(out, depth);
    out << (dynamic_cast<const ConstDeclAST*>(&decl) != nullptr ? "ConstDecl " : "VarDecl ")
        << decl.name() << '\n';
    dumpExpr(decl.initializer(), out, depth + 1);
}

void dumpStmt(const StmtAST& stmt, std::ostream& out, int depth) {
    indent(out, depth);
    if (const auto* block = dynamic_cast<const BlockAST*>(&stmt)) {
        out << "Block\n";
        for (const auto& child : block->statements()) {
            dumpStmt(*child, out, depth + 1);
        }
    } else if (const auto* decl = dynamic_cast<const DeclStmtAST*>(&stmt)) {
        out << "DeclStmt\n";
        dumpDecl(decl->declaration(), out, depth + 1);
    } else if (const auto* assign = dynamic_cast<const AssignStmtAST*>(&stmt)) {
        out << "AssignStmt " << assign->name() << '\n';
        dumpExpr(assign->value(), out, depth + 1);
    } else if (const auto* expr = dynamic_cast<const ExprStmtAST*>(&stmt)) {
        out << "ExprStmt\n";
        if (expr->expression() != nullptr) {
            dumpExpr(*expr->expression(), out, depth + 1);
        }
    } else if (const auto* ifStmt = dynamic_cast<const IfStmtAST*>(&stmt)) {
        out << "IfStmt\n";
        dumpExpr(ifStmt->condition(), out, depth + 1);
        dumpStmt(ifStmt->thenBranch(), out, depth + 1);
        if (ifStmt->elseBranch() != nullptr) {
            dumpStmt(*ifStmt->elseBranch(), out, depth + 1);
        }
    } else if (const auto* whileStmt = dynamic_cast<const WhileStmtAST*>(&stmt)) {
        out << "WhileStmt\n";
        dumpExpr(whileStmt->condition(), out, depth + 1);
        dumpStmt(whileStmt->body(), out, depth + 1);
    } else if (dynamic_cast<const BreakStmtAST*>(&stmt) != nullptr) {
        out << "BreakStmt\n";
    } else if (dynamic_cast<const ContinueStmtAST*>(&stmt) != nullptr) {
        out << "ContinueStmt\n";
    } else if (const auto* ret = dynamic_cast<const ReturnStmtAST*>(&stmt)) {
        out << "ReturnStmt\n";
        if (ret->value() != nullptr) {
            dumpExpr(*ret->value(), out, depth + 1);
        }
    }
}

} // namespace

void dumpAST(const CompUnitAST& ast, std::ostream& out) {
    out << "CompUnit\n";
    for (const auto& declaration : ast.declarations()) {
        dumpDecl(*declaration, out, 1);
    }
    for (const auto& function : ast.functions()) {
        indent(out, 1);
        out << "FuncDef " << (function->returnType() == ValueType::Int ? "int " : "void ")
            << function->name() << '\n';
        for (const auto& parameter : function->parameters()) {
            indent(out, 2);
            out << "Param " << parameter->name() << '\n';
        }
        dumpStmt(function->body(), out, 2);
    }
}

} // namespace toyc::ast
