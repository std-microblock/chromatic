add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

set_languages("c++23")

includes("deps/breeze-js.lua")
add_requires("breeze-js-runtime")
add_requires("capstone", "fmt")

target("chromatic-core")
    set_kind("static")
    add_files("src/core/**.cc")
    add_packages("breeze-js-runtime", "fmt", "capstone")
    add_headerfiles("src/core/**.h", "src/core/**.hpp")
    add_includedirs("src", {
        public = true
    })

target("chromatic-test")
    set_kind("binary")
    add_files("src/test/**.cc")
    add_deps("chromatic-core")