#include "frontend/parser.h"

#include <sstream>

namespace toyc::frontend {

Parser::Parser(std::string source) : lexer_(std::move(source)) {
    while (true) {
        tokens_.push_back(lexer_.nextToken());
        if (tokens_.back().kind == TokenKind::End) {
            break;
        }
    }
}

std::unique_ptr<ast::CompUnitAST> Parser::parseProgram() {
    auto program = std::make_unique<ast::CompUnitAST>();

    while (!check(TokenKind::End)) {
        if (check(TokenKind::KwConst)) {
            program->addDeclaration(parseDeclaration());
            continue;
        }

        if (!isTypeStart()) {
            errorAtCurrent("expected declaration or function definition");
        }

        const ast::ValueType type = parseType();
        Token name = consume(TokenKind::Identifier, "expected identifier after type");

        if (match(TokenKind::LParen)) {
            program->addFunction(parseFunction(type, std::move(name)));
        } else {
            if (type != ast::ValueType::Int) {
                throw ParseError("global variable declarations must use int at " +
                                 std::to_string(name.location.line) + ":" +
                                 std::to_string(name.location.column));
            }
            consume(TokenKind::Assign, "expected '=' in variable declaration");
            auto initializer = parseExpression();
            consume(TokenKind::Semicolon, "expected ';' after variable declaration");
            program->addDeclaration(std::make_unique<ast::VarDeclAST>(
                name.location, std::move(name.lexeme), std::move(initializer)));
        }
    }

    return program;
}

const Token& Parser::current() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::check(TokenKind kind) const {
    return current().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    ++current_;
    return true;
}

Token Parser::consume(TokenKind kind, const std::string& message) {
    if (!check(kind)) {
        errorAtCurrent(message + ", got '" + tokenKindName(current().kind) + "'");
    }
    return tokens_[current_++];
}

void Parser::errorAtCurrent(const std::string& message) const {
    std::ostringstream oss;
    oss << "parse error at " << current().location.line << ':' << current().location.column
        << ": " << message;
    throw ParseError(oss.str());
}

bool Parser::isTypeStart() const {
    return check(TokenKind::KwInt) || check(TokenKind::KwVoid);
}

bool Parser::isDeclarationStart() const {
    return check(TokenKind::KwConst) || check(TokenKind::KwInt);
}

ast::ValueType Parser::parseType() {
    if (match(TokenKind::KwInt)) {
        return ast::ValueType::Int;
    }
    if (match(TokenKind::KwVoid)) {
        return ast::ValueType::Void;
    }
    errorAtCurrent("expected type");
}

std::unique_ptr<ast::DeclAST> Parser::parseDeclaration() {
    if (match(TokenKind::KwConst)) {
        const Token start = previous();
        consume(TokenKind::KwInt, "expected 'int' after 'const'");
        Token name = consume(TokenKind::Identifier, "expected constant name");
        consume(TokenKind::Assign, "expected '=' in constant declaration");
        auto initializer = parseExpression();
        consume(TokenKind::Semicolon, "expected ';' after constant declaration");
        return std::make_unique<ast::ConstDeclAST>(start.location, std::move(name.lexeme),
                                                   std::move(initializer));
    }

    Token type = consume(TokenKind::KwInt, "expected 'int' in variable declaration");
    Token name = consume(TokenKind::Identifier, "expected variable name");
    consume(TokenKind::Assign, "expected '=' in variable declaration");
    auto initializer = parseExpression();
    consume(TokenKind::Semicolon, "expected ';' after variable declaration");
    return std::make_unique<ast::VarDeclAST>(type.location, std::move(name.lexeme),
                                             std::move(initializer));
}

std::unique_ptr<ast::FuncDefAST> Parser::parseFunction(ast::ValueType returnType, Token name) {
    std::vector<std::unique_ptr<ast::ParamAST>> parameters;
    if (!check(TokenKind::RParen)) {
        do {
            parameters.push_back(parseParam());
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "expected ')' after function parameters");
    auto body = parseBlock();
    return std::make_unique<ast::FuncDefAST>(name.location, returnType, std::move(name.lexeme),
                                             std::move(parameters), std::move(body));
}

std::unique_ptr<ast::ParamAST> Parser::parseParam() {
    const Token type = consume(TokenKind::KwInt, "expected 'int' in parameter");
    Token name = consume(TokenKind::Identifier, "expected parameter name");
    return std::make_unique<ast::ParamAST>(type.location, std::move(name.lexeme));
}

std::unique_ptr<ast::BlockAST> Parser::parseBlock() {
    Token start = consume(TokenKind::LBrace, "expected '{' to start block");
    auto block = std::make_unique<ast::BlockAST>(start.location);
    while (!check(TokenKind::RBrace) && !check(TokenKind::End)) {
        block->addStatement(parseStatement());
    }
    consume(TokenKind::RBrace, "expected '}' after block");
    return block;
}

std::unique_ptr<ast::StmtAST> Parser::parseStatement() {
    if (check(TokenKind::LBrace)) {
        return parseBlock();
    }

    if (match(TokenKind::Semicolon)) {
        return std::make_unique<ast::ExprStmtAST>(previous().location, nullptr);
    }

    if (isDeclarationStart()) {
        const auto loc = current().location;
        return std::make_unique<ast::DeclStmtAST>(loc, parseDeclaration());
    }

    if (match(TokenKind::KwIf)) {
        const auto loc = previous().location;
        consume(TokenKind::LParen, "expected '(' after 'if'");
        auto condition = parseExpression();
        consume(TokenKind::RParen, "expected ')' after if condition");
        auto thenBranch = parseStatement();
        std::unique_ptr<ast::StmtAST> elseBranch;
        if (match(TokenKind::KwElse)) {
            elseBranch = parseStatement();
        }
        return std::make_unique<ast::IfStmtAST>(loc, std::move(condition), std::move(thenBranch),
                                                std::move(elseBranch));
    }

    if (match(TokenKind::KwWhile)) {
        const auto loc = previous().location;
        consume(TokenKind::LParen, "expected '(' after 'while'");
        auto condition = parseExpression();
        consume(TokenKind::RParen, "expected ')' after while condition");
        auto body = parseStatement();
        return std::make_unique<ast::WhileStmtAST>(loc, std::move(condition), std::move(body));
    }

    if (match(TokenKind::KwBreak)) {
        const auto loc = previous().location;
        consume(TokenKind::Semicolon, "expected ';' after 'break'");
        return std::make_unique<ast::BreakStmtAST>(loc);
    }

    if (match(TokenKind::KwContinue)) {
        const auto loc = previous().location;
        consume(TokenKind::Semicolon, "expected ';' after 'continue'");
        return std::make_unique<ast::ContinueStmtAST>(loc);
    }

    if (match(TokenKind::KwReturn)) {
        const auto loc = previous().location;
        std::unique_ptr<ast::ExprAST> value;
        if (!check(TokenKind::Semicolon)) {
            value = parseExpression();
        }
        consume(TokenKind::Semicolon, "expected ';' after return statement");
        return std::make_unique<ast::ReturnStmtAST>(loc, std::move(value));
    }

    if (check(TokenKind::Identifier) && tokens_[current_ + 1].kind == TokenKind::Assign) {
        return parseStatementAfterIdentifier();
    }

    const auto loc = current().location;
    auto expression = parseExpression();
    consume(TokenKind::Semicolon, "expected ';' after expression statement");
    return std::make_unique<ast::ExprStmtAST>(loc, std::move(expression));
}

std::unique_ptr<ast::StmtAST> Parser::parseStatementAfterIdentifier() {
    Token name = consume(TokenKind::Identifier, "expected assignment target");
    consume(TokenKind::Assign, "expected '=' in assignment");
    auto value = parseExpression();
    consume(TokenKind::Semicolon, "expected ';' after assignment");
    return std::make_unique<ast::AssignStmtAST>(name.location, std::move(name.lexeme),
                                                std::move(value));
}

std::unique_ptr<ast::ExprAST> Parser::parseExpression() {
    return parseLogicalOr();
}

std::unique_ptr<ast::ExprAST> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();
    while (match(TokenKind::PipePipe)) {
        const Token op = previous();
        auto rhs = parseLogicalAnd();
        expr = std::make_unique<ast::BinaryExprAST>(op.location, op.lexeme, std::move(expr),
                                                    std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ast::ExprAST> Parser::parseLogicalAnd() {
    auto expr = parseRelational();
    while (match(TokenKind::AmpAmp)) {
        const Token op = previous();
        auto rhs = parseRelational();
        expr = std::make_unique<ast::BinaryExprAST>(op.location, op.lexeme, std::move(expr),
                                                    std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ast::ExprAST> Parser::parseRelational() {
    auto expr = parseAdditive();
    while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
           match(TokenKind::GreaterEqual) || match(TokenKind::EqualEqual) ||
           match(TokenKind::BangEqual)) {
        const Token op = previous();
        auto rhs = parseAdditive();
        expr = std::make_unique<ast::BinaryExprAST>(op.location, op.lexeme, std::move(expr),
                                                    std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ast::ExprAST> Parser::parseAdditive() {
    auto expr = parseMultiplicative();
    while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
        const Token op = previous();
        auto rhs = parseMultiplicative();
        expr = std::make_unique<ast::BinaryExprAST>(op.location, op.lexeme, std::move(expr),
                                                    std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ast::ExprAST> Parser::parseMultiplicative() {
    auto expr = parseUnary();
    while (match(TokenKind::Star) || match(TokenKind::Slash) || match(TokenKind::Percent)) {
        const Token op = previous();
        auto rhs = parseUnary();
        expr = std::make_unique<ast::BinaryExprAST>(op.location, op.lexeme, std::move(expr),
                                                    std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ast::ExprAST> Parser::parseUnary() {
    if (match(TokenKind::Plus) || match(TokenKind::Minus) || match(TokenKind::Bang)) {
        const Token op = previous();
        auto operand = parseUnary();
        return std::make_unique<ast::UnaryExprAST>(op.location, op.lexeme, std::move(operand));
    }
    return parsePrimary();
}

std::unique_ptr<ast::ExprAST> Parser::parsePrimary() {
    if (match(TokenKind::Number)) {
        const Token number = previous();
        return std::make_unique<ast::NumberExprAST>(number.location, number.numberValue);
    }

    if (match(TokenKind::Identifier)) {
        Token name = previous();
        if (match(TokenKind::LParen)) {
            std::vector<std::unique_ptr<ast::ExprAST>> arguments;
            if (!check(TokenKind::RParen)) {
                do {
                    arguments.push_back(parseExpression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "expected ')' after call arguments");
            return std::make_unique<ast::CallExprAST>(name.location, std::move(name.lexeme),
                                                      std::move(arguments));
        }
        return std::make_unique<ast::IdentifierExprAST>(name.location, std::move(name.lexeme));
    }

    if (match(TokenKind::LParen)) {
        auto expr = parseExpression();
        consume(TokenKind::RParen, "expected ')' after expression");
        return expr;
    }

    errorAtCurrent("expected expression");
}

std::unique_ptr<ast::CompUnitAST> parseProgram(const std::string& source) {
    Parser parser(source);
    return parser.parseProgram();
}

} // namespace toyc::frontend
