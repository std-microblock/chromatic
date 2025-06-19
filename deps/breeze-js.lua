package("breeze-js")
    set_description("A lightweight and modern JavaScript runtime built on QuickJS for desktop applications.")
    set_license("AGPL-3.0")

    add_urls("https://github.com/breeze-shell/breeze-js.git")

    add_versions("latest", "master")

    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install("windows|!arm", "linux", "macosx", function (package)
        io.replace("xmake.lua", [[set_languages("c89", "c++20")]], [[set_languages("c11", "c++20")]], {plain = true})
        io.replace("xmake.lua", [[set_kind("binary")]], [[set_kind("binary")
set_enabled(false)]], {plain = true})
        io.replace("xmake.lua", [[set_kind("static")]], [[set_kind("static")
if is_plat("macosx") then
    add_cxxflags("-fexperimental-library")
end]], {plain = true})
        import("package.tools.xmake").install(package)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <quickjs.h>
            void test() {
                auto runtime = JS_NewRuntime();
            }
        ]]}, {configs = {languages = "c++20"}}))
    end)