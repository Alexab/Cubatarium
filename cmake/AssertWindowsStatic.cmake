# Fail configure early if the Windows desktop build is not static (/MT + x64-windows-static).
if(NOT WIN32)
    return()
endif()

if(NOT VCPKG_TARGET_TRIPLET MATCHES "-static$")
    message(FATAL_ERROR
        "Windows desktop requires vcpkg triplet x64-windows-static (got '${VCPKG_TARGET_TRIPLET}').\n"
        "VS Code / Cursor: CMake: Delete Cache and Reconfigure, preset 'windows-msvc'.\n"
        "Terminal: .\\configure.ps1 -Config Release -Fresh")
endif()

if(VCPKG_APPLOCAL_DEPS)
    message(FATAL_ERROR
        "VCPKG_APPLOCAL_DEPS must be OFF for a static desktop build (installer copies DLLs next to exe).")
endif()
