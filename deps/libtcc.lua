package("libtcc")
    set_homepage("https://bellard.org/tcc/")
    set_description("Tiny C Compiler")

    set_urls("https://github.com/std-microblock/tinycc.git")
    add_versions("2026.03.30+3", "8b83545011b3790f216189a6916a4cbf9b0212ed")

    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install(function(package)
        import("package.tools.xmake").install(package)
    end)