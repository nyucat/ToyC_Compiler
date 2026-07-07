#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace toyc::ast {

struct SourceLocation {
    int line = 1;
    int column = 1;
};

enum class ValueType {
    Int,
    Void,
};

struct Symbol;

class ASTNode {
public:
    explicit ASTNode(SourceLocation location) : location_(location) {}
    virtual ~ASTNode() = default;

    [[nodiscard]] SourceLocation location() const { return location_; }

private:
    SourceLocation location_;
};

class ExprAST : public ASTNode {
public:
    using ASTNode::ASTNode;
};

class NumberExprAST final : public ExprAST {
public:
    NumberExprAST(SourceLocation location, int value);

    [[nodiscard]] int value() const { return value_; }

private:
    int value_;
};

class IdentifierExprAST final : public ExprAST {
public:
    IdentifierExprAST(SourceLocation location, std::string name);

    [[nodiscard]] const std::string& name() const { return name_; }
    Symbol* resolvedSymbol = nullptr;

private:
    std::string name_;
};

class UnaryExprAST final : public ExprAST {
public:
    UnaryExprAST(SourceLocation location, std::string op, std::unique_ptr<ExprAST> operand);

    [[nodiscard]] const std::string& op() const { return op_; }
    [[nodiscard]] const ExprAST& operand() const { return *operand_; }
    [[nodiscard]] ExprAST& operand() { return *operand_; }

private:
    std::string op_;
    std::unique_ptr<ExprAST> operand_;
};

class BinaryExprAST final : public ExprAST {
public:
    BinaryExprAST(SourceLocation location, std::string op, std::unique_ptr<ExprAST> lhs,
                  std::unique_ptr<ExprAST> rhs);

    [[nodiscard]] const std::string& op() const { return op_; }
    [[nodiscard]] const ExprAST& lhs() const { return *lhs_; }
    [[nodiscard]] const ExprAST& rhs() const { return *rhs_; }
    [[nodiscard]] ExprAST& lhs() { return *lhs_; }
    [[nodiscard]] ExprAST& rhs() { return *rhs_; }

private:
    std::string op_;
    std::unique_ptr<ExprAST> lhs_;
    std::unique_ptr<ExprAST> rhs_;
};

class CallExprAST final : public ExprAST {
public:
    CallExprAST(SourceLocation location, std::string callee,
                std::vector<std::unique_ptr<ExprAST>> arguments);

    [[nodiscard]] const std::string& callee() const { return callee_; }
    [[nodiscard]] const std::vector<std::unique_ptr<ExprAST>>& arguments() const { return arguments_; }
    Symbol* resolvedSymbol = nullptr;

private:
    std::string callee_;
    std::vector<std::unique_ptr<ExprAST>> arguments_;
};

class StmtAST : public ASTNode {
public:
    using ASTNode::ASTNode;
};

class DeclAST : public ASTNode {
public:
    DeclAST(SourceLocation location, std::string name, std::unique_ptr<ExprAST> initializer);

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const ExprAST& initializer() const { return *initializer_; }
    [[nodiscard]] ExprAST& initializer() { return *initializer_; }
    Symbol* resolvedSymbol = nullptr;

private:
    std::string name_;
    std::unique_ptr<ExprAST> initializer_;
};

class VarDeclAST final : public DeclAST {
public:
    using DeclAST::DeclAST;
};

class ConstDeclAST final : public DeclAST {
public:
    using DeclAST::DeclAST;
};

class DeclStmtAST final : public StmtAST {
public:
    DeclStmtAST(SourceLocation location, std::unique_ptr<DeclAST> declaration);

    [[nodiscard]] const DeclAST& declaration() const { return *declaration_; }
    [[nodiscard]] DeclAST& declaration() { return *declaration_; }

private:
    std::unique_ptr<DeclAST> declaration_;
};

class AssignStmtAST final : public StmtAST {
public:
    AssignStmtAST(SourceLocation location, std::string name, std::unique_ptr<ExprAST> value);

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const ExprAST& value() const { return *value_; }
    [[nodiscard]] ExprAST& value() { return *value_; }
    Symbol* resolvedSymbol = nullptr;

private:
    std::string name_;
    std::unique_ptr<ExprAST> value_;
};

class ExprStmtAST final : public StmtAST {
public:
    ExprStmtAST(SourceLocation location, std::unique_ptr<ExprAST> expression);

    [[nodiscard]] const ExprAST* expression() const { return expression_.get(); }
    [[nodiscard]] ExprAST* expression() { return expression_.get(); }

private:
    std::unique_ptr<ExprAST> expression_;
};

class BlockAST final : public StmtAST {
public:
    explicit BlockAST(SourceLocation location);

    void addStatement(std::unique_ptr<StmtAST> statement);
    [[nodiscard]] const std::vector<std::unique_ptr<StmtAST>>& statements() const { return statements_; }

private:
    std::vector<std::unique_ptr<StmtAST>> statements_;
};

class IfStmtAST final : public StmtAST {
public:
    IfStmtAST(SourceLocation location, std::unique_ptr<ExprAST> condition,
              std::unique_ptr<StmtAST> thenBranch, std::unique_ptr<StmtAST> elseBranch);

    [[nodiscard]] const ExprAST& condition() const { return *condition_; }
    [[nodiscard]] const StmtAST& thenBranch() const { return *thenBranch_; }
    [[nodiscard]] const StmtAST* elseBranch() const { return elseBranch_.get(); }

private:
    std::unique_ptr<ExprAST> condition_;
    std::unique_ptr<StmtAST> thenBranch_;
    std::unique_ptr<StmtAST> elseBranch_;
};

class WhileStmtAST final : public StmtAST {
public:
    WhileStmtAST(SourceLocation location, std::unique_ptr<ExprAST> condition,
                 std::unique_ptr<StmtAST> body);

    [[nodiscard]] const ExprAST& condition() const { return *condition_; }
    [[nodiscard]] const StmtAST& body() const { return *body_; }

private:
    std::unique_ptr<ExprAST> condition_;
    std::unique_ptr<StmtAST> body_;
};

class BreakStmtAST final : public StmtAST {
public:
    using StmtAST::StmtAST;
};

class ContinueStmtAST final : public StmtAST {
public:
    using StmtAST::StmtAST;
};

class ReturnStmtAST final : public StmtAST {
public:
    ReturnStmtAST(SourceLocation location, std::unique_ptr<ExprAST> value);

    [[nodiscard]] const ExprAST* value() const { return value_.get(); }
    [[nodiscard]] ExprAST* value() { return value_.get(); }

private:
    std::unique_ptr<ExprAST> value_;
};

class ParamAST final : public ASTNode {
public:
    ParamAST(SourceLocation location, std::string name);

    [[nodiscard]] const std::string& name() const { return name_; }
    Symbol* resolvedSymbol = nullptr;

private:
    std::string name_;
};

class FuncDefAST final : public ASTNode {
public:
    FuncDefAST(SourceLocation location, ValueType returnType, std::string name,
               std::vector<std::unique_ptr<ParamAST>> parameters, std::unique_ptr<BlockAST> body);

    [[nodiscard]] ValueType returnType() const { return returnType_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::vector<std::unique_ptr<ParamAST>>& parameters() const { return parameters_; }
    [[nodiscard]] const BlockAST& body() const { return *body_; }
    Symbol* resolvedSymbol = nullptr;

private:
    ValueType returnType_;
    std::string name_;
    std::vector<std::unique_ptr<ParamAST>> parameters_;
    std::unique_ptr<BlockAST> body_;
};

class CompUnitAST final : public ASTNode {
public:
    CompUnitAST();

    void addDeclaration(std::unique_ptr<DeclAST> declaration);
    void addFunction(std::unique_ptr<FuncDefAST> function);

    [[nodiscard]] const std::vector<std::unique_ptr<DeclAST>>& declarations() const { return declarations_; }
    [[nodiscard]] const std::vector<std::unique_ptr<FuncDefAST>>& functions() const { return functions_; }

private:
    std::vector<std::unique_ptr<DeclAST>> declarations_;
    std::vector<std::unique_ptr<FuncDefAST>> functions_;
};

void dumpAST(const CompUnitAST& ast, std::ostream& out);

} // namespace toyc::ast
