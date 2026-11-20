// Grav plugin for JetBrains IDEs (CLion, IntelliJ, …).
// Build:   ./gradlew buildPlugin     -> build/distributions/grav-jetbrains-*.zip
// Run:     ./gradlew runIde          (launches a sandbox IDE with the plugin)
//
// Requires JDK 17+ and network access (the IntelliJ Platform SDK is downloaded
// on first build).

plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "2.0.21"
    id("org.jetbrains.intellij.platform") version "2.1.0"
}

group = "dev.grav"
version = "0.10.0"

repositories {
    mavenCentral()
    intellijPlatform {
        defaultRepositories()
    }
}

dependencies {
    intellijPlatform {
        // Target CLion; swap to intellijIdeaCommunity("2024.2") to test in IDEA.
        // `-PlocalIde=/path/to/clion` builds against an installed IDE instead.
        val localIde = providers.gradleProperty("localIde").orNull
        if (localIde != null) local(localIde) else clion("2025.2")
    }
}

intellijPlatform {
    // Kotlin only, no forms or settings pages: nothing to instrument or index.
    instrumentCode = false
    buildSearchableOptions = false
    pluginConfiguration {
        id = "dev.grav.lang"
        name = "Grav"
        version = project.version.toString()
        ideaVersion {
            sinceBuild = "242"   // 2024.2+
            untilBuild = provider { null as String? }
        }
    }
}

kotlin {
    jvmToolchain(21)
    // A newer installed IDE ships newer Kotlin metadata than this compiler reads.
    if (providers.gradleProperty("localIde").isPresent) {
        compilerOptions.freeCompilerArgs.add("-Xskip-metadata-version-check")
    }
}
