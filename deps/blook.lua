package("blook")
    add_deps("cmake")
    add_syslinks("advapi32")
    set_sourcedir(path.join(os.scriptdir(), "blook"))
    on_install(function (package)
        local fcdir = package:cachedir() .. "/fetchcontent"
        import("package.tools.cmake").install(package, {
                "-DCMAKE_INSTALL_PREFIX=" .. package:installdir(),
                "-DCMAKE_PREFIX_PATH=" .. package:installdir(),
                "-DFETCHCONTENT_QUIET=OFF",
                "-DFETCHCONTENT_BASE_DIR=" .. fcdir,
        })
        
        os.cp("include/blook/**", package:installdir("include/blook/"))
        os.cp("external/zasm/zasm/include/**", package:installdir("include/zasm/"))
        os.cp(fcdir .. "/zydis-src/dependencies/zycore/include/**", package:installdir("include/zycore/"))
        os.cp(package:buildir() .. "/blook.lib", package:installdir("lib"))
        os.cp(package:buildir() .. "/external/zasm/zasm.lib", package:installdir("lib"))
    end)
package_end()