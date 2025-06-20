set_project("chromatic")
set_policy("compatibility.version", "3.0")

set_languages("c++23")
set_warnings("all")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

add_rules("mode.releasedbg")

includes("deps/blook.lua")
includes("deps/breeze-js.lua")
includes("deps/cpp-ipc.lua")

add_requires("yalantinglibs 0c98464dd202aaa6275a8da3297719a436b8a51a", {
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

add_requires("blook", "breeze-js", "reflect-cpp", "chromatic-cpp-ipc", "cpptrace v0.8.3", "gtest")
set_runtimes("MT")

target("chromatic_ipc")
    set_kind("static")
    add_defines("NOMINMAX")
    add_packages("yalantinglibs", "reflect-cpp", "chromatic-cpp-ipc", {
        public = true,
    })
    add_files("ipc/ipc.cc")
    add_headerfiles("ipc/ipc.h")
    add_includedirs("ipc", {public = true})
    set_encodings("utf-8")

target("chromatic")
    set_kind("shared")
    add_defines("NOMINMAX")
    add_packages("blook", "breeze-js", "reflect-cpp", "yalantinglibs", "chromatic-cpp-ipc", "cpptrace")
    add_syslinks("oleacc", "ole32", "oleaut32", "uuid", "comctl32", "comdlg32", "gdi32", "user32", "shell32", "kernel32", "advapi32", "psapi")
    add_files("src/**/*.cc", "src/*.cc")
    remove_files("src/ipc.cc")
    add_deps("chromatic_ipc")
    set_encodings("utf-8")

target("chromatic_ipc_test")
    set_kind("binary")
    add_deps("chromatic_ipc")
    add_packages("gtest", "yalantinglibs")
    add_files("test/ipc_test.cc")
    add_includedirs("src")
    set_encodings("utf-8")