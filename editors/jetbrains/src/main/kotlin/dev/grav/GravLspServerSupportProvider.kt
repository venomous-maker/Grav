package dev.grav

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.configurations.PathEnvironmentVariableUtil
import com.intellij.notification.Notification
import com.intellij.notification.NotificationType
import com.intellij.notification.Notifications
import com.intellij.openapi.application.PathManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspServerSupportProvider
import com.intellij.platform.lsp.api.ProjectWideLspServerDescriptor
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Connects the IDE to the dependency-free Grav language server (server.js and
 * grav-index.js, bundled in this plugin) for go-to-definition, references,
 * rename, document symbols, hover, completion, inlay hints and compiler
 * diagnostics.
 *
 * Needs Node.js (found on PATH, or where nvm, fnm, Volta or a package manager
 * put it, since an IDE started from the desktop does not see a shell's PATH) and
 * an IDE with the LSP API (CLion, IntelliJ Ultimate, ...).
 */
class GravLspServerSupportProvider : LspServerSupportProvider {
    override fun fileOpened(
        project: Project,
        file: VirtualFile,
        serverStarter: LspServerSupportProvider.LspServerStarter
    ) {
        if (file.extension != "grav") return
        if (GravTools.node() == null) {
            GravTools.warnOnce("Grav: Node.js was not found",
                "Go to definition, rename and the other navigation need Node.js. Install it, or put it on the PATH the IDE starts with, then reopen the project.")
            return
        }
        serverStarter.ensureServerStarted(GravLspServerDescriptor(project))
    }
}

class GravLspServerDescriptor(project: Project) :
    ProjectWideLspServerDescriptor(project, "Grav") {

    override fun isSupportedFile(file: VirtualFile): Boolean = file.extension == "grav"

    override fun createCommandLine(): GeneralCommandLine {
        val node = GravTools.node() ?: error("Node.js was not found")
        val cmd = GeneralCommandLine(node.absolutePath, GravTools.server(project).absolutePath)
            .withWorkDirectory(project.basePath)
            .withParentEnvironmentType(GeneralCommandLine.ParentEnvironmentType.CONSOLE)
        GravTools.compiler()?.let { cmd.withEnvironment("GRAV_COMPILER", it.absolutePath) }
        return cmd
    }
}

internal object GravTools {
    private val warned = AtomicBoolean(false)
    private val home = System.getProperty("user.home")

    /** The project's own editors/lsp/server.js (working on Grav itself), else the bundled one. */
    fun server(project: Project): File {
        project.basePath?.let { base -> File("$base/editors/lsp/server.js").takeIf { it.isFile }?.let { return it } }
        return bundled()
    }

    /** The bundled server, unpacked into the IDE's system folder (again when a new plugin brings another). */
    @Synchronized
    private fun bundled(): File {
        val dir = File(PathManager.getSystemPath(), "grav-lsp")
        for (name in listOf("server.js", "grav-index.js")) {
            val bytes = GravTools::class.java.getResourceAsStream("/grav-lsp/$name")?.use { it.readBytes() }
                ?: error("the plugin is missing grav-lsp/$name")
            val out = File(dir, name)
            if (!out.isFile || out.length() != bytes.size.toLong() || !out.readBytes().contentEquals(bytes)) {
                dir.mkdirs()
                out.writeBytes(bytes)
            }
        }
        return File(dir, "server.js")
    }

    /** Node.js: on PATH, else the newest one nvm, fnm or Volta installed, else the usual places. */
    fun node(): File? = find("node", listOf(
        newestUnder("$home/.nvm/versions/node", "bin/node"),
        newestUnder("$home/.local/share/fnm/node-versions", "installation/bin/node"),
        "$home/.volta/bin/node", "$home/.local/bin/node",
        "/usr/local/bin/node", "/usr/bin/node", "/opt/homebrew/bin/node", "/snap/bin/node",
    ))

    /** The Grav compiler: on PATH, else where install.sh puts it. */
    fun compiler(): File? = find("grav", listOf("$home/.local/bin/grav", "/usr/local/bin/grav", "/opt/homebrew/bin/grav"))

    private fun find(name: String, fallbacks: List<String?>): File? =
        PathEnvironmentVariableUtil.findInPath(name)
            ?: fallbacks.filterNotNull().map(::File).firstOrNull { it.isFile && it.canExecute() }

    /** `<dir>/<version>/<rest>` for the highest version (v24.7.0 over v18.20.1). */
    private fun newestUnder(dir: String, rest: String): String? {
        val versions = File(dir).listFiles { f -> f.isDirectory } ?: return null
        val key = { f: File -> f.name.trimStart('v').split('.').map { it.toIntOrNull() ?: 0 } }
        val newest = versions.sortedWith { a, b ->
            val x = key(a); val y = key(b)
            (0 until maxOf(x.size, y.size)).map { (x.getOrElse(it) { 0 }).compareTo(y.getOrElse(it) { 0 }) }.firstOrNull { it != 0 } ?: 0
        }.lastOrNull() ?: return null
        return File(newest, rest).path
    }

    fun warnOnce(title: String, text: String) {
        if (warned.compareAndSet(false, true))
            Notifications.Bus.notify(Notification("Grav", title, text, NotificationType.WARNING))
    }
}
