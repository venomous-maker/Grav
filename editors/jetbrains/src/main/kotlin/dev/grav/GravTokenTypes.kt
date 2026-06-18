package dev.grav

import com.intellij.psi.tree.IElementType

class GravTokenType(debugName: String) : IElementType(debugName, GravLanguage)

object GravTypes {
    val LINE_COMMENT = GravTokenType("LINE_COMMENT")
    val BLOCK_COMMENT = GravTokenType("BLOCK_COMMENT")
    val STRING = GravTokenType("STRING")
    val NUMBER = GravTokenType("NUMBER")
    val KEYWORD = GravTokenType("KEYWORD")
    val TYPE = GravTokenType("TYPE")
    val CONSTANT = GravTokenType("CONSTANT")
    val BUILTIN = GravTokenType("BUILTIN")
    val IDENTIFIER = GravTokenType("IDENTIFIER")
    val OPERATOR = GravTokenType("OPERATOR")
    val DECORATOR = GravTokenType("DECORATOR")
    val PUNCTUATION = GravTokenType("PUNCTUATION")
}
