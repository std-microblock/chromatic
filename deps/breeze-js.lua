
local BREEZE_JS_VERSION = "2026.03.11"
local BREEZE_JS_HASH = "a8252754b59333463defaa316f4a9d567237c893"

package("breeze-quickjs-ng")
    set_description("The breeze-quickjs-ng package")
    set_urls("https://github.com/breeze-shell/breeze-js.git")
    add_versions(BREEZE_JS_VERSION, BREEZE_JS_HASH)
    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install(function (package)
        import("package.tools.xmake").install(package)
    end)


package("breeze-js-runtime")
    set_description("The breeze-js-runtime package")
    set_urls("https://github.com/breeze-shell/breeze-js.git")
    add_versions(BREEZE_JS_VERSION, BREEZE_JS_HASH)
    add_deps("breeze-quickjs-ng", {public=true})
    add_deps("yalantinglibs 0c98464dd202aaa6275a8da3297719a436b8a51a", {configs={ssl=true}})
    add_deps("ctre")

    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install(function (package)
        import("package.tools.xmake").install(package)
    end)
