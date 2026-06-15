#include "lexer/token.h"

namespace grav {

const char *tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::Identifier: return "identifier";
        case TokenType::IntLiteral: return "int literal";
        case TokenType::FloatLiteral: return "float literal";
        case TokenType::StringLiteral: return "string literal";
        case TokenType::KwInt: return "'int'";
        case TokenType::KwFloat: return "'float'";
        case TokenType::KwBool: return "'bool'";
        case TokenType::KwString: return "'string'";
        case TokenType::KwVoid: return "'void'";
        case TokenType::True: return "'true'";
        case TokenType::False: return "'false'";
        case TokenType::Let: return "'let'";
        case TokenType::Const: return "'const'";
        case TokenType::Fn: return "'fn'";
        case TokenType::Class: return "'class'";
        case TokenType::Struct: return "'struct'";
        case TokenType::Abstract: return "'abstract'";
        case TokenType::Interface: return "'interface'";
        case TokenType::Namespace: return "'namespace'";
        case TokenType::Constructor: return "'constructor'";
        case TokenType::Extends: return "'extends'";
        case TokenType::Implements: return "'implements'";
        case TokenType::New: return "'new'";
        case TokenType::Return: return "'return'";
        case TokenType::Static: return "'static'";
        case TokenType::Readonly: return "'readonly'";
        case TokenType::Public: return "'public'";
        case TokenType::Private: return "'private'";
        case TokenType::Protected: return "'protected'";
        case TokenType::Self: return "'self'";
        case TokenType::If: return "'if'";
        case TokenType::Else: return "'else'";
        case TokenType::While: return "'while'";
        case TokenType::For: return "'for'";
        case TokenType::Do: return "'do'";
        case TokenType::Switch: return "'switch'";
        case TokenType::Case: return "'case'";
        case TokenType::Default: return "'default'";
        case TokenType::Match: return "'match'";
        case TokenType::Break: return "'break'";
        case TokenType::Continue: return "'continue'";
        case TokenType::Colon: return "':'";
        case TokenType::Semicolon: return "';'";
        case TokenType::Comma: return "','";
        case TokenType::Dot: return "'.'";
        case TokenType::Assign: return "'='";
        case TokenType::Arrow: return "'->'";
        case TokenType::LParen: return "'('";
        case TokenType::RParen: return "')'";
        case TokenType::LBrace: return "'{'";
        case TokenType::RBrace: return "'}'";
        case TokenType::Plus: return "'+'";
        case TokenType::Minus: return "'-'";
        case TokenType::Star: return "'*'";
        case TokenType::Slash: return "'/'";
        case TokenType::PlusPlus: return "'++'";
        case TokenType::MinusMinus: return "'--'";
        case TokenType::EqEq: return "'=='";
        case TokenType::NotEq: return "'!='";
        case TokenType::Greater: return "'>'";
        case TokenType::Less: return "'<'";
        case TokenType::GreaterEq: return "'>='";
        case TokenType::LessEq: return "'<='";
        case TokenType::AmpAmp: return "'&&'";
        case TokenType::PipePipe: return "'||'";
        case TokenType::Bang: return "'!'";
        case TokenType::EndOfFile: return "end of file";
    }
    return "token";
}

} // namespace grav
