#ifndef GRAV_TOKEN_H
#define GRAV_TOKEN_H

#include <string>

namespace grav {

enum class TokenType {
    // literals / identifiers
    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,

    // type / value keywords
    KwInt,
    KwFloat,
    KwBool,
    KwString,
    KwVoid,
    True,
    False,

    // declaration keywords
    Let,
    Const,
    Fn,
    Class,
    Struct,
    Abstract,
    Interface,
    Namespace,
    Constructor,
    Extends,
    Implements,
    New,
    Return,
    Static,
    Readonly,
    Public,
    Private,
    Protected,
    Self,   // self / this
    If,
    Else,
    While,
    For,
    Do,
    Switch,
    Case,
    Default,
    Match,
    Break,
    Continue,

    // punctuation
    Colon,
    Semicolon,
    Comma,
    Dot,
    Assign,     // =
    Arrow,      // ->
    LParen,
    RParen,
    LBrace,
    RBrace,

    // arithmetic
    Plus,
    Minus,
    Star,
    Slash,
    PlusPlus,   // ++
    MinusMinus, // --

    // comparison
    EqEq,       // ==
    NotEq,      // !=
    Greater,    // >
    Less,       // <
    GreaterEq,  // >=
    LessEq,     // <=

    // logical
    AmpAmp,     // &&
    PipePipe,   // ||
    Bang,       // !

    EndOfFile,
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int col;
};

const char *tokenTypeName(TokenType type);

} // namespace grav

#endif // GRAV_TOKEN_H
