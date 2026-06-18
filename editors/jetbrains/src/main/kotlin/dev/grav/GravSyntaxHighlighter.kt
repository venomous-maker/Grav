package dev.grav

import com.intellij.lexer.Lexer
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors
import com.intellij.openapi.editor.HighlighterColors
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighter
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

class GravSyntaxHighlighter : SyntaxHighlighterBase() {
    override fun getHighlightingLexer(): Lexer = GravLexer()

    override fun getTokenHighlights(tokenType: IElementType): Array<TextAttributesKey> {
        val key = when (tokenType) {
            GravTypes.LINE_COMMENT -> LINE_COMMENT
            GravTypes.BLOCK_COMMENT -> BLOCK_COMMENT
            GravTypes.STRING -> STRING
            GravTypes.NUMBER -> NUMBER
            GravTypes.KEYWORD -> KEYWORD
            GravTypes.TYPE -> TYPE
            GravTypes.CONSTANT -> CONSTANT
            GravTypes.BUILTIN -> BUILTIN
            GravTypes.DECORATOR -> DECORATOR
            GravTypes.OPERATOR -> OPERATOR
            TokenType.BAD_CHARACTER -> BAD_CHARACTER
            else -> null
        }
        return if (key != null) arrayOf(key) else emptyArray()
    }

    companion object {
        private fun key(name: String, fallback: TextAttributesKey) =
            TextAttributesKey.createTextAttributesKey(name, fallback)

        val LINE_COMMENT = key("GRAV_LINE_COMMENT", DefaultLanguageHighlighterColors.LINE_COMMENT)
        val BLOCK_COMMENT = key("GRAV_BLOCK_COMMENT", DefaultLanguageHighlighterColors.BLOCK_COMMENT)
        val STRING = key("GRAV_STRING", DefaultLanguageHighlighterColors.STRING)
        val NUMBER = key("GRAV_NUMBER", DefaultLanguageHighlighterColors.NUMBER)
        val KEYWORD = key("GRAV_KEYWORD", DefaultLanguageHighlighterColors.KEYWORD)
        val TYPE = key("GRAV_TYPE", DefaultLanguageHighlighterColors.KEYWORD)
        val CONSTANT = key("GRAV_CONSTANT", DefaultLanguageHighlighterColors.CONSTANT)
        val BUILTIN = key("GRAV_BUILTIN", DefaultLanguageHighlighterColors.FUNCTION_CALL)
        val DECORATOR = key("GRAV_DECORATOR", DefaultLanguageHighlighterColors.METADATA)
        val OPERATOR = key("GRAV_OPERATOR", DefaultLanguageHighlighterColors.OPERATION_SIGN)
        val BAD_CHARACTER = key("GRAV_BAD_CHARACTER", HighlighterColors.BAD_CHARACTER)
    }
}

class GravSyntaxHighlighterFactory : SyntaxHighlighterFactory() {
    override fun getSyntaxHighlighter(project: Project?, virtualFile: VirtualFile?): SyntaxHighlighter =
        GravSyntaxHighlighter()
}
