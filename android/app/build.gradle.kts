import java.security.MessageDigest

plugins {
    id("com.android.application")
}

val thirdParty = rootProject.file("third_party")
val sdlJava = File(thirdParty, "SDL3-3.4.16/android-project/app/src/main/java")

android {
    namespace = "io.divby0.rich4"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "io.divby0.rich4.mod"
        minSdk = 28 // bionic iconv/libc baseline; Big5 itself comes from bundled GNU libiconv
        targetSdk = 36
        // Sideload-only spike. Play rule: plain integer, +1 per upload, never derived.
        versionCode = 1
        versionName = "0.1.0-MOD"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
                targets += listOf("main", "SDL3-shared")
            }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildTypes {
        debug {
            // Optimised native code even in debug: the game simulates and renders every frame on the CPU.
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets["main"].java.srcDir(sdlJava)
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/rich4Assets"))
    // Launcher icon from the original rich4.exe, written by scripts/make_android_icon.py (never committed).
    sourceSets["main"].res.srcDir(layout.buildDirectory.dir("generated/rich4Res"))
}

// Packs the original game files (never committed) into assets/rich4-data/ of the APK.
// rich4_android.cpp extracts them to internal storage whenever the manifest stamp changes.
val prepareRich4Assets by tasks.registering {
    val source = providers.gradleProperty("rich4.gameAssets").orElse(rootProject.file("../game-assets").path)
    val font = providers.gradleProperty("rich4.font")
    val soundfont = File(thirdParty, "downloads/GeneralUser-GS.sf2")
    val output = layout.buildDirectory.dir("generated/rich4Assets/rich4-data")
    inputs.dir(source)
    inputs.property("font", font.orElse(""))
    inputs.file(soundfont)
    outputs.dir(output)
    doLast {
        val target = output.get().asFile
        target.deleteRecursively()
        val names = mutableListOf<String>()
        val digest = MessageDigest.getInstance("SHA-256")
        fun add(file: File, name: String) {
            file.copyTo(File(target, name), overwrite = true)
            names += name
            digest.update("$name:${file.length()}:${file.lastModified()}\n".toByteArray())
        }
        File(source.get()).listFiles()!!.sortedBy { it.name }
            .filter { it.isFile && it.extension.lowercase() in setOf("mkf", "mid", "wav") }
            .forEach { add(it, "game/${it.name}") }
        require(names.any { it.equals("game/Data.mkf", ignoreCase = true) }) { "No Data.mkf in ${source.get()}" }
        add(soundfont, "GeneralUser-GS.sf2")
        if (font.isPresent) add(File(font.get()), "font.otf")
        val stamp = digest.digest().joinToString("") { "%02x".format(it) }.take(16)
        File(target, "manifest.txt").writeText((listOf(stamp) + names).joinToString("\n", postfix = "\n"))
    }
}
tasks.named("preBuild") { dependsOn(prepareRich4Assets) }
