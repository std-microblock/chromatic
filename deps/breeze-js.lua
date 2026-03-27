local BREEZE_JS_VERSION = "2026.03.27+9"
local BREEZE_JS_HASH = "14f75809f713b23256386b1c93f04048b910b107"
local BREEZE_JS_SOURCEDIR = path.join(os.scriptdir(), "../", "../GitHub/breeze-js")
local USE_LOCAL = os.exists(BREEZE_JS_SOURCEDIR)

if USE_LOCAL then
    print("Using local breeze-js source directory: " .. BREEZE_JS_SOURCEDIR)
end

package("breeze-quickjs-ng")
    set_description("The breeze-quickjs-ng package")
    if USE_LOCAL then
        set_sourcedir(BREEZE_JS_SOURCEDIR)
    else
        set_urls("https://github.com/breeze-shell/breeze-js.git")
        add_versions(BREEZE_JS_VERSION, BREEZE_JS_HASH)
    end
    add_configs("shared", { description = "Build shared library.", default = false, type = "boolean", readonly = true })

    on_install(function(package)
        import("package.tools.xmake").install(package)
    end)


package("breeze-js-runtime")
    set_description("The breeze-js-runtime package")
    if USE_LOCAL then
        set_sourcedir(BREEZE_JS_SOURCEDIR)
    else
        set_urls("https://github.com/breeze-shell/breeze-js.git")
        add_versions(BREEZE_JS_VERSION, BREEZE_JS_HASH)
    end
    add_deps("breeze-quickjs-ng", { public = true })
    add_deps("yalantinglibs 0c98464dd202aaa6275a8da3297719a436b8a51a", { configs = { ssl = true } })
    add_deps("ctre")
    add_deps("concurrentqueue", { public = true })

    add_configs("shared", { description = "Build shared library.", default = false, type = "boolean", readonly = true })

    on_install(function(package)
        import("package.tools.xmake").install(package)
    end)
