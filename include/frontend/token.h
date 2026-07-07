#pragma once

#include "ast/ast.h"

#include <string>

namespace toyc::frontend {

enum class TokenKind {
    End,
    Identifier,
    Number,

    KwConst,
    KwInt,
    KwVoid,
    KwIf,
    KwElse,
    KwWhile,
    KwBreak,
    KwContinue,
    KwReturn,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    AmpAmp,
    PipePipe,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Assign,

    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semicolon,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string lexeme;
    int numberValue = 0;
    ast::SourceLocation location;
};

std::string tokenKindName(TokenKind kind);

} // namespace toyc::frontend
