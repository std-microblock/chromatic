set_project("chromatic")
set_policy("compatibility.version", "3.0")

set_languages("c++23")
set_warnings("all")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

add_rules("mode.releasedbg")

includes("deps/blook.lua")
includes("deps/breeze-js.lua")
includes("deps/cpp-ipc.lua")

add_requires("yalantinglibs b82a21925958b6c50deba3aa26a2737cdb814e27", {
    configs = {
        ssl = true
    }
})

add_requireconfs("**.cinatra", {
    override = true,
    version = "e329293f6705649a6f1e8847ec845a7631179bb8"
})

add_requireconfs("**.async_simple", {
    override = true,
    version = "18f3882be354d407af0f0674121dcddaeff36e26"
})

add_requires("blook", "breeze-js a52a734a11257824ab03932155c97149ecb450ac", "reflect-cpp", "chromatic-cpp-ipc", "cpptrace")
set_runtimes("MT")

target("chromatic")
    set_kind("shared")
    add_defines("NOMINMAX")
    add_defines("_HAS_CXX23=1", "_HAS_CXX20=1", "_HAS_CXX17=1")
    add_packages("blook", "breeze-js", "reflect-cpp", "yalantinglibs", "chromatic-cpp-ipc", "cpptrace")
    add_syslinks("oleacc", "ole32", "oleaut32", "uuid", "comctl32", "comdlg32", "gdi32", "user32", "shell32", "kernel32", "advapi32", "psapi")
    add_files("src/**/*.cc", "src/*.cc")
    set_encodings("utf-8")
