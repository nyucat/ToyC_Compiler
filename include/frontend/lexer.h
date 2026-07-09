#pragma once

#include "frontend/token.h"

#include <string>

namespace toyc::frontend {

class Lexer {
public:
    explicit Lexer(std::string source);

    Token nextToken();

private:
    [[nodiscard]] bool isAtEnd() const;
    [[nodiscard]] char peek(int offset = 0) const;
    char advance();
    bool match(char expected);
    void skipWhitespaceAndComments();
    Token makeToken(TokenKind kind, ast::SourceLocation location, std::string lexeme) const;
    Token identifier(ast::SourceLocation location, std::string lexeme);
    Token number(ast::SourceLocation location, std::string lexeme);

    std::string source_;
    std::size_t current_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace toyc::frontend
