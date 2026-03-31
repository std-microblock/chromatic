add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

set_languages("c++23")

includes("deps/yalantinglibs.lua")
includes("deps/breeze-js.lua")
includes("deps/libtcc.lua")

add_requires("breeze-js-runtime")
add_requires("capstone", "fmt", "libffi", "asmjit", "gtest")
add_requires("xz", "reflect-cpp")
add_requires("libtcc")

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

if is_os("linux") or is_os("android") then
    add_cxflags("-fPIC")
    add_ldflags("-fPIC")
end

-- Architecture defines
if is_arch("arm64", "aarch64", "arm64-v8a") then
    add_defines("CHROMATIC_ARM64")
elseif is_arch("x86_64", "x64") then
    add_defines("CHROMATIC_X64")
end

target("chromatic-core")
    set_kind("static")
    add_files("src/core/**.cc")
    add_rules("utils.bin2obj", {extensions = ".js"})
    add_files("src/core/typescript/dist/index.js")
    add_packages("breeze-js-runtime", "fmt", "capstone", "libffi", "asmjit", "libtcc", {
        public = true
    })
    add_headerfiles("src/core/**.h", "src/core/**.hpp")
    add_includedirs("src", {
        public = true
    })

    -- Platform link libraries
    if is_os("windows") then
        add_syslinks("kernel32", "psapi", "user32", "dbghelp")
    elseif is_os("linux") then
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
    -- On Linux, breeze-quickjs-ng.a references JS_EnqueueJob defined in
    -- breeze-js-runtime.a; wrap in --start-group/--end-group to resolve.
    if is_os("linux") then
        add_linkgroups("breeze-js-runtime", "breeze-quickjs-ng", {group = true})
    end

target("chromatic-injectee")
    set_kind("shared")
    add_files("src/injectee/**.cc")
    add_deps("chromatic-core")
    add_packages("xz", "reflect-cpp", "fmt")
    if is_os("linux") then
        add_linkgroups("breeze-js-runtime", "breeze-quickjs-ng", {group = true})
    end
