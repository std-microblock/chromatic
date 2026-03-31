package("libtcc")
    set_homepage("https://bellard.org/tcc/")
    set_description("Tiny C Compiler")

    set_urls("https://github.com/std-microblock/tinycc.git")
    add_versions("2026.03.30+4", "90b56dc59a8c68d2ce97d327b5647c198d0cb67f")

    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install(function(package)
        import("package.tools.xmake").install(package)
    end)