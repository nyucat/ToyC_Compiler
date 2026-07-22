#pragma once

#include "ast/ast.h"
#include "frontend/lexer.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace toyc::frontend {

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

class Parser {
public:
    explicit Parser(std::string source);

    std::unique_ptr<ast::CompUnitAST> parseProgram();

private:
    const Token& current() const;
    const Token& previous() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    Token consume(TokenKind kind, const std::string& message);
    [[noreturn]] void errorAtCurrent(const std::string& message) const;

    bool isTypeStart() const;
    bool isDeclarationStart() const;
    ast::ValueType parseType();
    std::unique_ptr<ast::DeclAST> parseDeclaration();
    std::unique_ptr<ast::FuncDefAST> parseFunction(ast::ValueType returnType, Token name);
    std::unique_ptr<ast::ParamAST> parseParam();
    std::unique_ptr<ast::BlockAST> parseBlock();
    std::unique_ptr<ast::StmtAST> parseStatement();
    std::unique_ptr<ast::StmtAST> parseStatementAfterIdentifier();

    std::unique_ptr<ast::ExprAST> parseExpression();
    std::unique_ptr<ast::ExprAST> parseLogicalOr();
    std::unique_ptr<ast::ExprAST> parseLogicalAnd();
    std::unique_ptr<ast::ExprAST> parseRelational();
    std::unique_ptr<ast::ExprAST> parseAdditive();
    std::unique_ptr<ast::ExprAST> parseMultiplicative();
    std::unique_ptr<ast::ExprAST> parseUnary();
    std::unique_ptr<ast::ExprAST> parsePrimary();

    Lexer lexer_;
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
};

std::unique_ptr<ast::CompUnitAST> parseProgram(const std::string& source);

} // namespace toyc::frontend
