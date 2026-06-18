# Grav for JetBrains IDEs (CLion / IntelliJ)

Two ways to get Grav support in CLion, depending on whether you want it *now* or
want a real, distributable plugin.

## A) Install now — TextMate bundle (no build, works in CLion today)

CLion has built-in TextMate support, and it reuses the same grammar as the VS Code
extension. To install the [`textmate/`](textmate/) bundle:

1. **Settings → Editor → TextMate Bundles**
2. Click **+** and select this folder:
   `…/Practice/editors/jetbrains/textmate`
3. **Apply / OK**, then reopen a `.grav` file.

That gives full syntax highlighting (keywords, the widened numeric / `binary`
types, `super`, `typedef`, strings + `${…}`, inline-C `%{ … %}`, comments).

> Auto-registering this by editing CLion's config file isn't safe while CLion is
> running (it rewrites `options/textmate.xml` on exit), so use the dialog above —
> it takes about ten seconds and is the supported path.

## B) Build the real plugin (for the Marketplace / sharing)

A complete Gradle [IntelliJ Platform](https://plugins.jetbrains.com/docs/intellij/)
plugin lives here ([`build.gradle.kts`](build.gradle.kts),
[`src/main/kotlin/dev/grav/`](src/main/kotlin/dev/grav)): a `Language`, `FileType`,
a hand-written lexer, a syntax highlighter, and a commenter.

**Requirements:** JDK 17+ and network access (the IntelliJ Platform SDK is
downloaded on first build). This box has only JDK 11, so build it on a machine
with JDK 17+, or open the `editors/jetbrains` folder in IntelliJ IDEA (it
provisions Gradle automatically).

```bash
cd editors/jetbrains
gradle wrapper            # once, if you don't already have ./gradlew
./gradlew buildPlugin     # -> build/distributions/grav-jetbrains-0.9.0.zip
./gradlew runIde          # or: launch a sandbox IDE with the plugin loaded
```

Install the built zip in CLion via **Settings → Plugins → ⚙ → Install Plugin from
Disk…**, pick the zip, and restart.

The plugin targets CLion `2025.2` by default (`clion("2025.2")` in
`build.gradle.kts`); change that coordinate to target another IDE/version.
