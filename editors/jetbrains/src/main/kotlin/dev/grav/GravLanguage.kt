package dev.grav

import com.intellij.lang.Language
import com.intellij.openapi.fileTypes.LanguageFileType
import javax.swing.Icon

object GravLanguage : Language("Grav")

object GravFileType : LanguageFileType(GravLanguage) {
    override fun getName(): String = "Grav file"
    override fun getDescription(): String = "Grav language file"
    override fun getDefaultExtension(): String = "grav"
    override fun getIcon(): Icon? = null
}
