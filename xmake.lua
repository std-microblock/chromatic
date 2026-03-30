add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

set_languages("c++23")

includes("deps/breeze-js.lua")
add_requires("breeze-js-runtime")
add_requires("capstone", "fmt", "libffi", "asmjit", "gtest")
add_requires("xz", "reflect-cpp")

-- Platform defines
if is_os("windows") then
    add_defines("CHROMATIC_WINDOWS")
elseif is_os("linux") then
    add_defines("CHROMATIC_LINUX")
elseif is_os("macosx") then
    add_defines("CHROMATIC_DARWIN")
elseif is_os("android") then
    add_defines("CHROMATIC_ANDROID")
end

-- Architecture defines
if is_arch("arm64", "aarch64") then
    add_defines("CHROMATIC_ARM64")
elseif is_arch("x86_64", "x64") then
    add_defines("CHROMATIC_X64")
end

target("chromatic-core")
    set_kind("static")
    add_files("src/core/**.cc")
    add_rules("utils.bin2obj", {extensions = ".js"})
    add_files("src/core/typescript/dist/index.js")
    add_packages("breeze-js-runtime", "fmt", "capstone", "libffi", "asmjit", {
        public = true
    })
    add_headerfiles("src/core/**.h", "src/core/**.hpp")
    add_includedirs("src", {
        public = true
    })

    -- Platform link libraries
    if is_os("windows") then
        add_syslinks("kernel32", "psapi", "user32", "dbghelp")
    elseif is_os("linux") or is_os("android") then
        add_syslinks("dl", "pthread")
    elseif is_os("macosx") then
        add_syslinks("dl", "pthread")
        add_frameworks("CoreFoundation", "CoreServices")
    end

target("chromatic-test")
    set_kind("binary")
    add_files("src/test/**.cc")
    add_deps("chromatic-core")
    add_packages("gtest")

target("chromatic-injectee")
    set_kind("shared")
    add_files("src/injectee/**.cc")
    add_deps("chromatic-core")
    add_packages("xz", "reflect-cpp", "fmt")
