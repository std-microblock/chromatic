package("libtcc")
    set_homepage("https://bellard.org/tcc/")
    set_description("Tiny C Compiler")

    set_urls("https://github.com/std-microblock/tinycc.git")
    add_versions("2026.03.30+1", "38caec5487b44da82ed9b25699e13cdab8845b77")

    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install(function(package)
        import("package.tools.xmake").install(package)
    end)