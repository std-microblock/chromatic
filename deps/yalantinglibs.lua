package("cinatra")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/qicosmos/cinatra")
    set_description("modern c++(c++20), cross-platform, header-only, easy to use http framework")
    set_license("MIT")

    add_urls("https://github.com/std-microblock/cinatra.git")

    add_configs("ssl", {description = "Enable SSL", default = false, type = "boolean"})
    add_configs("gzip", {description = "Enable GZIP", default = false, type = "boolean"})
    add_configs("sse42", {description = "Enable sse4.2 instruction set", default = false, type = "boolean"})
    add_configs("avx2", {description = "Enable avx2 instruction set", default = false, type = "boolean"})
    add_configs("aarch64", {description = "Enable aarch64 instruction set (only arm)", default = false, type = "boolean"})

    add_deps("asio")
    add_deps("async_simple 18f3882be354d407af0f0674121dcddaeff36e26", {configs = {aio = false}})

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

    add_versions("2026.03.30", "0c98464dd202aaa6275a8da3297719a436b8a51a")

    add_configs("ssl", {description = "Enable ssl support", default = false, type = "boolean"})
    add_configs("pmr", {description = "Enable pmr support",  default = false, type = "boolean"})
    add_configs("io_uring", {description = "Enable io_uring",  default = false, type = "boolean"})
    add_configs("file_io_uring", {description = "Enable file io_uring",  default = false, type = "boolean"})
    add_configs("struct_pack_unportable_type", {description = "enable struct_pack unportable type(like wchar_t)",  default = false, type = "boolean"})
    add_configs("struct_pack_unportable_optimize", {description = "enable struct_pack optimize(but cost more compile time)",  default = false, type = "boolean"})

    add_deps("cmake")
    add_deps("cinatra d9485603c89d1fbb286f459f4d3dfdf4b44a04df", "iguana", {
        configs = {
            ssl = true
        }
    })

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
