package dev.grav

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspServerSupportProvider
import com.intellij.platform.lsp.api.ProjectWideLspServerDescriptor
import java.io.File

/**
 * Connects CLion to the dependency-free Grav language server
 * (editors/lsp/server.js) for go-to-definition, references, rename, document
 * symbols, hover, and compiler diagnostics.
 *
 * Requires Node.js on PATH and a paid IDE with the LSP API (CLion, IntelliJ
 * Ultimate, …). Community IDEs don't ship the LSP API, so this provider simply
 * does nothing there — syntax highlighting still works.
 */
class GravLspServerSupportProvider : LspServerSupportProvider {
    override fun fileOpened(
        project: Project,
        file: VirtualFile,
        serverStarter: LspServerSupportProvider.LspServerStarter
    ) {
        if (file.extension == "grav" && locateServer(project) != null) {
            serverStarter.ensureServerStarted(GravLspServerDescriptor(project))
        }
    }
}

internal fun locateServer(project: Project): File? {
    val base = project.basePath ?: return null
    return File("$base/editors/lsp/server.js").takeIf { it.isFile }
}

class GravLspServerDescriptor(project: Project) :
    ProjectWideLspServerDescriptor(project, "Grav") {

    override fun isSupportedFile(file: VirtualFile): Boolean = file.extension == "grav"

    override fun createCommandLine(): GeneralCommandLine {
        val server = locateServer(project)
            ?: error("Grav language server not found at <project>/editors/lsp/server.js")
        return GeneralCommandLine("node", server.absolutePath)
            .withWorkDirectory(project.basePath)
    }
}
