#include "frontend/lexer.h"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace toyc::frontend {

namespace {

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierContinue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

} // namespace

std::string tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::End: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Number: return "number";
    case TokenKind::KwConst: return "const";
    case TokenKind::KwInt: return "int";
    case TokenKind::KwVoid: return "void";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwElse: return "else";
    case TokenKind::KwWhile: return "while";
    case TokenKind::KwBreak: return "break";
    case TokenKind::KwContinue: return "continue";
    case TokenKind::KwReturn: return "return";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::Bang: return "!";
    case TokenKind::AmpAmp: return "&&";
    case TokenKind::PipePipe: return "||";
    case TokenKind::EqualEqual: return "==";
    case TokenKind::BangEqual: return "!=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::Assign: return "=";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::Comma: return ",";
    case TokenKind::Semicolon: return ";";
    }
    return "unknown";
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

Token Lexer::nextToken() {
    skipWhitespaceAndComments();

    const ast::SourceLocation location{line_, column_};
    if (isAtEnd()) {
        return makeToken(TokenKind::End, location, "");
    }

    const char c = advance();
    const std::string one(1, c);

    if (isIdentifierStart(c)) {
        std::string lexeme = one;
        while (isIdentifierContinue(peek())) {
            lexeme.push_back(advance());
        }
        return identifier(location, std::move(lexeme));
    }

    if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
        std::string lexeme = one;
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            lexeme.push_back(advance());
        }
        return number(location, std::move(lexeme));
    }

    switch (c) {
    case '+': return makeToken(TokenKind::Plus, location, one);
    case '-': return makeToken(TokenKind::Minus, location, one);
    case '*': return makeToken(TokenKind::Star, location, one);
    case '/': return makeToken(TokenKind::Slash, location, one);
    case '%': return makeToken(TokenKind::Percent, location, one);
    case '(': return makeToken(TokenKind::LParen, location, one);
    case ')': return makeToken(TokenKind::RParen, location, one);
    case '{': return makeToken(TokenKind::LBrace, location, one);
    case '}': return makeToken(TokenKind::RBrace, location, one);
    case ',': return makeToken(TokenKind::Comma, location, one);
    case ';': return makeToken(TokenKind::Semicolon, location, one);
    case '!':
        if (match('=')) {
            return makeToken(TokenKind::BangEqual, location, "!=");
        }
        return makeToken(TokenKind::Bang, location, one);
    case '&':
        if (match('&')) {
            return makeToken(TokenKind::AmpAmp, location, "&&");
        }
        break;
    case '|':
        if (match('|')) {
            return makeToken(TokenKind::PipePipe, location, "||");
        }
        break;
    case '=':
        if (match('=')) {
            return makeToken(TokenKind::EqualEqual, location, "==");
        }
        return makeToken(TokenKind::Assign, location, one);
    case '<':
        if (match('=')) {
            return makeToken(TokenKind::LessEqual, location, "<=");
        }
        return makeToken(TokenKind::Less, location, one);
    case '>':
        if (match('=')) {
            return makeToken(TokenKind::GreaterEqual, location, ">=");
        }
        return makeToken(TokenKind::Greater, location, one);
    default:
        break;
    }

    throw std::runtime_error("unexpected character '" + one + "' at " + std::to_string(location.line) +
                             ":" + std::to_string(location.column));
}

bool Lexer::isAtEnd() const {
    return current_ >= source_.size();
}

char Lexer::peek(int offset) const {
    const std::size_t index = current_ + static_cast<std::size_t>(offset);
    if (index >= source_.size()) {
        return '\0';
    }
    return source_[index];
}

char Lexer::advance() {
    const char c = source_[current_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[current_] != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    bool consumed = true;
    while (consumed && !isAtEnd()) {
        consumed = false;
        while (std::isspace(static_cast<unsigned char>(peek())) != 0) {
            advance();
            consumed = true;
        }

        if (peek() == '/' && peek(1) == '/') {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            consumed = true;
        } else if (peek() == '/' && peek(1) == '*') {
            advance();
            advance();
            while (!(peek() == '*' && peek(1) == '/')) {
                if (isAtEnd()) {
                    throw std::runtime_error("unterminated block comment");
                }
                advance();
            }
            advance();
            advance();
            consumed = true;
        }
    }
}

Token Lexer::makeToken(TokenKind kind, ast::SourceLocation location, std::string lexeme) const {
    return Token{kind, std::move(lexeme), 0, location};
}

Token Lexer::identifier(ast::SourceLocation location, std::string lexeme) {
    static const std::unordered_map<std::string, TokenKind> keywords{
        {"const", TokenKind::KwConst},       {"int", TokenKind::KwInt},
        {"void", TokenKind::KwVoid},         {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},         {"while", TokenKind::KwWhile},
        {"break", TokenKind::KwBreak},       {"continue", TokenKind::KwContinue},
        {"return", TokenKind::KwReturn},
    };

    if (const auto it = keywords.find(lexeme); it != keywords.end()) {
        return makeToken(it->second, location, std::move(lexeme));
    }
    return makeToken(TokenKind::Identifier, location, std::move(lexeme));
}

Token Lexer::number(ast::SourceLocation location, std::string lexeme) {
    long long value = 0;
    for (const char c : lexeme) {
        value = value * 10 + (c - '0');
        if (value > std::numeric_limits<int>::max()) {
            throw std::runtime_error("integer literal out of range at " + std::to_string(location.line) +
                                     ":" + std::to_string(location.column));
        }
    }

    Token token = makeToken(TokenKind::Number, location, std::move(lexeme));
    token.numberValue = static_cast<int>(value);
    return token;
}

} // namespace toyc::frontend
