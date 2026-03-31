package("libtcc")
    set_homepage("https://bellard.org/tcc/")
    set_description("Tiny C Compiler")

    set_urls("https://github.com/std-microblock/tinycc.git")
    add_versions("2026.03.30+2", "4aa71afa8e59194d7b05f5ea49ff9476401b7d20")

    add_configs("shared", {description = "Build shared library.", default = false, type = "boolean", readonly = true})

    on_install(function(package)
        import("package.tools.xmake").install(package)
    end)