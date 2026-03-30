package("cinatra")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/qicosmos/cinatra")
    set_description("modern c++(c++20), cross-platform, header-only, easy to use http framework")
    set_license("MIT")

    add_urls("https://github.com/qicosmos/cinatra/archive/refs/tags/$(version).tar.gz",
             "https://github.com/qicosmos/cinatra.git")

    add_versions("0.9.5", "f21448332f56484af76347a3da30099dcd155d31ac9d3098faf7e9d6cb1e0c34")
    add_versions("0.9.4", "2b8b4e264f8083674554db55ca137998f02c528730cf9565697234fec9de3378")
    add_versions("0.9.1", "d1a8018e41caabbda2c380175b632e3c9c10b519727f6b998eda4e3f4ede84bd")
    add_versions("v0.8.9", "007dc38aceedf42d03a9c05dc9aa6d2f303456ae7ce1100800df7a565b83b510")
    add_versions("v0.8.0", "4e14d5206408eccb43b3e810d3a1fe228fbc7496ded8a16b041ed12cbcce4479")

    add_patches(">=0.8.9 <=0.9.2", "patches/0.8.9/windows-move.patch", "c913ed0e9044ffc0ced40516245ec0d55262f8eabd30244d9911c3f0427a60f5")

    add_configs("ssl", {description = "Enable SSL", default = false, type = "boolean"})
    add_configs("gzip", {description = "Enable GZIP", default = false, type = "boolean"})
    add_configs("sse42", {description = "Enable sse4.2 instruction set", default = false, type = "boolean"})
    add_configs("avx2", {description = "Enable avx2 instruction set", default = false, type = "boolean"})
    add_configs("aarch64", {description = "Enable aarch64 instruction set (only arm)", default = false, type = "boolean"})

    add_deps("asio")
    add_deps("async_simple", {configs = {aio = false}})

    on_check("windows", function (package)
        local vs_toolset = package:toolchain("msvc"):config("vs_toolset")
        if vs_toolset then
            local vs_toolset_ver = import("core.base.semver").new(vs_toolset)
            local minor = vs_toolset_ver:minor()
            assert(minor and minor >= 30, "package(cinatra) require vs_toolset >= 14.3")
        end
    end)

    on_load(function (package)
        package:add("defines", "ASIO_STANDALONE")
        if package:config("ssl") then
            package:add("deps", "openssl")
            package:add("defines", "CINATRA_ENABLE_SSL")
        end
        if package:config("gzip") then
            package:add("deps", "zlib")
            package:add("defines", "CINATRA_ENABLE_GZIP")
        end

        local configdeps = {
            sse42 = "CINATRA_SSE",
            avx2 = "CINATRA_AVX2",
            aarch64 = "CINATRA_ARM_OPT"
        }
        
        for name, item in pairs(configdeps) do
            if package:config(name) then
                package:add("defines", item)
            end
        end
    end)

    on_install("windows", "linux", "macosx", "android", function (package)
        os.cp("include", package:installdir())
    end)

    on_test(function (package)
        assert(package:has_cxxincludes("cinatra.hpp", {configs = {languages = "c++20"}}))
    end)

package("yalantinglibs")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/alibaba/yalantinglibs")
    set_description("A collection of modern C++ libraries")
    set_license("Apache-2.0")

    set_urls("https://github.com/alibaba/yalantinglibs/archive/refs/tags/$(version).tar.gz",
             "https://github.com/alibaba/yalantinglibs.git")

    add_versions("0.6.0", "798c32a0d25c6306d6ff3d9dbd1c561a41e16cc11bd30b8993c737f6ac0517d9")
    add_versions("0.5.8", "49b631c191f139ff465b489663a620f3ae24af6baa236cc1e3d96f3ac7506d73")
    add_versions("0.5.7", "1c1057289e5488f90dd326fd2bb9d3173bad11eb5b06bc0a8bf0fa80857e1cfa")
    add_versions("0.5.6", "b2656f794af30c5b83952b7c73c2dabf949061ddb6284d18d7f0c0560244b35a")
    add_versions("0.5.5", "7962579c1414d1ade4fd22316476723d54112c919514bf1e6015a1870e5e68f7")
    add_versions("0.5.3", "9d24612975d38fa4b4a05bd9f8f5cb65d447365e5eb3661d0eba9701d383523a")
    add_versions("0.5.2", "e63500b9b84b6efd76bfc375d0972c0376d98067f7a6118bfd9a3048d557f46a")
    add_versions("0.4.0", "35d88b5e329f88edb702c1c40a67dedb4438898774c96bb6f3f1704ab828257f")
    add_versions("0.3.11", "1766ca1ec977e2dd56dabdcad3172dc1b79c3bd1acd26ea2de019299fa7e888a")
    add_versions("0.3.9", "aea6c5c99297f9b875eac8cabdf846b8f8e792bf7ccb3da8e0afda90ea62f00b")
    add_versions("0.3.8", "a9966687a2ac1ed0b1a001a69e144db4cff4cdf77a5a80c00364e6ea687d3c52")
    add_versions("0.3.7", "b4258806173f63034aa529913601bc3d90da8a598725c0edf0be1a8c5c6f32b8")
    add_versions("0.3.6", "92f694ad42537f95535efc648fc5e73e82f840dae4f54524a096050db398214b")
    add_versions("0.3.4", "dd5edd3f43f23cd4b0614896e6587b61bb38c981dc21c85a54bcc54800d0dfe8")
    add_versions("0.3.5", "8d382573da01449c4f83fccbbc3bdc08d221651f3fc8b9137eb4fbdb703677c2")

    add_configs("ssl", {description = "Enable ssl support", default = false, type = "boolean"})
    add_configs("pmr", {description = "Enable pmr support",  default = false, type = "boolean"})
    add_configs("io_uring", {description = "Enable io_uring",  default = false, type = "boolean"})
    add_configs("file_io_uring", {description = "Enable file io_uring",  default = false, type = "boolean"})
    add_configs("struct_pack_unportable_type", {description = "enable struct_pack unportable type(like wchar_t)",  default = false, type = "boolean"})
    add_configs("struct_pack_unportable_optimize", {description = "enable struct_pack optimize(but cost more compile time)",  default = false, type = "boolean"})

    add_deps("cmake")
    add_deps("cinatra", "iguana")

    on_check("windows", function (package)
        local vs_toolset = package:toolchain("msvc"):config("vs_toolset")
        if vs_toolset then
            local vs_toolset_ver = import("core.base.semver").new(vs_toolset)
            local minor = vs_toolset_ver:minor()
            assert(minor and minor >= 30, "package(yalantinglibs) dep(cinatra) require vs_toolset >= 14.3")
        end
    end)

    on_load(function (package)
        if package:config("ssl") then
            package:add("deps", "openssl")
            package:add("defines", "YLT_ENABLE_SSL")
        end
        if package:config("pmr") then
            package:add("defines", "YLT_ENABLE_PMR")
        end
        if package:config("io_uring") then
            package:add("deps", "liburing")
            package:add("defines", "ASIO_HAS_IO_URING", "ASIO_DISABLE_EPOLL", "ASIO_HAS_FILE", "YLT_ENABLE_FILE_IO_URING")
        end
        if package:config("file_io_uring") then
            package:add("deps", "liburing")
            package:add("defines", "ASIO_HAS_IO_URING", "ASIO_HAS_FILE", "YLT_ENABLE_FILE_IO_URING")
        end
        if package:config("struct_pack_unportable_type") then
            package:add("defines", "STRUCT_PACK_ENABLE_UNPORTABLE_TYPE")
        end
        if package:config("struct_pack_unportable_optimize") then
            package:add("defines", "YLT_ENABLE_STRUCT_PACK_OPTIMIZE")
        end
    end)

    on_install("windows", "linux", "macosx", "android", function (package)
        local configs = {
            "-DINSTALL_THIRDPARTY=OFF",
            "-DINSTALL_STANDALONE=OFF",
            "-DINSTALL_INDEPENDENT_THIRDPARTY=OFF",
            "-DINSTALL_INDEPENDENT_STANDALONE=OFF",
            "-DCMAKE_PROJECT_NAME=xmake",
        }
        for name, enabled in table.orderpairs(package:configs()) do
            if not package:extraconf("configs", name, "builtin") then
                table.insert(configs, "-DYLT_ENABLE_" .. name:upper() .. "=" .. (enabled and "ON" or "OFF"))
            end
        end
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include "ylt/struct_pack.hpp"
            struct person {
                int64_t id;
                std::string name;
                int age;
                double salary;
            };
            void test() {
                person person1{.id = 1, .name = "hello struct pack", .age = 20, .salary = 1024.42};
                std::vector<char> buffer = struct_pack::serialize(person1);
            }
        ]]}, {configs = {languages = "c++20"}}))
    end)