package("breeze-js")
    set_description("A lightweight and modern JavaScript runtime built on QuickJS for desktop applications.")
    set_license("AGPL-3.0")

    add_urls("https://github.com/breeze-shell/breeze-js.git")

    add_versions("20250621.3", "4a1556efe18e7afb4a13c3b25abe54080577cae7")
    add_deps("yalantinglibs 0c98464dd202aaa6275a8da3297719a436b8a51a", {
        configs = {
            ssl = true
        }
    })

    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install("windows|!arm", "linux", "macosx", function (package)
        io.replace("xmake.lua", [[set_languages("c89", "c++20")]], [[set_languages("c11", "c++20")]], {plain = true})
        io.replace("xmake.lua", [[set_kind("binary")]], [[set_kind("binary")
set_enabled(false)]], {plain = true})
        io.replace("xmake.lua", [[set_kind("static")]], [[set_kind("static")
add_defines("NDEBUG")
if is_plat("macosx") then
    add_cxxflags("-fexperimental-library")
end]], {plain = true})
        import("package.tools.xmake").install(package)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <breeze-js/quickjs.h>
            #include <breeze-js/script.h>
            void test() {
                auto ctx = std::make_shared<breeze::script_context>();
                ctx->reset_runtime();
            }
        ]]}, {configs = {languages = "c++23"}}))
    end)