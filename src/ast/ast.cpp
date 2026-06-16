#include "ast/ast.h"

namespace grav {

const char *binaryOpSymbol(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
        case BinaryOp::Mod: return "%";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr: return "|";
        case BinaryOp::BitXor: return "^";
        case BinaryOp::Shl: return "<<";
        case BinaryOp::Shr: return ">>";
        case BinaryOp::Eq: return "==";
        case BinaryOp::NotEq: return "!=";
        case BinaryOp::Greater: return ">";
        case BinaryOp::Less: return "<";
        case BinaryOp::GreaterEq: return ">=";
        case BinaryOp::LessEq: return "<=";
        case BinaryOp::And: return "&&";
        case BinaryOp::Or: return "||";
    }
    return "?";
}

bool isComparison(BinaryOp op) {
    switch (op) {
        case BinaryOp::Eq:
        case BinaryOp::NotEq:
        case BinaryOp::Greater:
        case BinaryOp::Less:
        case BinaryOp::GreaterEq:
        case BinaryOp::LessEq:
            return true;
        default:
            return false;
    }
}

bool isLogical(BinaryOp op) {
    return op == BinaryOp::And || op == BinaryOp::Or;
}

bool isIntOnly(BinaryOp op) {
    switch (op) {
        case BinaryOp::Mod:
        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::Shl:
        case BinaryOp::Shr:
            return true;
        default:
            return false;
    }
}

const char *accessName(Access a) {
    switch (a) {
        case Access::Public: return "public";
        case Access::Private: return "private";
        case Access::Protected: return "protected";
    }
    return "public";
}

} // namespace grav
