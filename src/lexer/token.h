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
    CBlock,     // %{ ... %} verbatim C escape hatch

    // type / value keywords
    KwInt,
    KwFloat,
    KwBool,
    KwString,
    KwVoid,
    True,
    False,
    Null,

    // declaration keywords
    Let,
    Const,
    Fn,
    Class,
    Struct,
    Enum,
    Type,       // `type Name = T;` alias
    Sizeof,     // sizeof(T) / sizeof(expr)
    Abstract,
    Interface,
    Async,
    Await,
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
    In,         // for-in ranges
    As,         // value cast: expr as int
    Is,         // RTTI test: expr is Shape
    Export,     // `export` decl modifier (parsed; no-op)

    // punctuation
    Colon,
    ColonColon, // ::
    Semicolon,
    Comma,
    Dot,
    DotDot,     // ..
    DotDotEq,   // ..=
    Ellipsis,   // ...
    Assign,     // =
    Arrow,      // ->
    FatArrow,   // =>
    At,         // @
    Hash,       // #
    Question,   // ?
    QuestionQuestion, // ??
    QuestionDot,      // ?.
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,   // [
    RBracket,   // ]

    // arithmetic
    Plus,
    Minus,
    Star,
    Slash,
    Percent,    // %
    PlusPlus,   // ++
    MinusMinus, // --

    // compound assignment
    PlusEq,     // +=
    MinusEq,    // -=
    StarEq,     // *=
    SlashEq,    // /=
    PercentEq,  // %=

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

    // bitwise
    Amp,        // &
    Pipe,       // |
    Caret,      // ^
    Tilde,      // ~
    ShiftLeft,  // <<
    ShiftRight, // >>

    // compound bitwise assignment
    AmpEq,        // &=
    PipeEq,       // |=
    CaretEq,      // ^=
    ShiftLeftEq,  // <<=
    ShiftRightEq, // >>=

    // lexer recovery / future tooling
    Unknown,
    Invalid,

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
