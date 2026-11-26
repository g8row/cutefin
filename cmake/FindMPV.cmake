# FindMPV.cmake - locate libmpv for the embedded render-API player.
#
# Tries pkg-config first (the common case on Linux distros and Homebrew),
# then falls back to a manual search rooted at MPV_ROOT / CMAKE_PREFIX_PATH
# so a locally-built libmpv can be picked up too.
#
# Defines, on success:
#   MPV_FOUND
#   MPV_INCLUDE_DIRS
#   MPV_LIBRARIES
# and an imported target MPV::MPV.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_MPV QUIET mpv)
endif()

find_path(MPV_INCLUDE_DIR
    NAMES mpv/client.h
    HINTS ${PC_MPV_INCLUDEDIR} ${PC_MPV_INCLUDE_DIRS} ${MPV_ROOT}
    PATH_SUFFIXES include
)

find_library(MPV_LIBRARY
    NAMES mpv libmpv
    HINTS ${PC_MPV_LIBDIR} ${PC_MPV_LIBRARY_DIRS} ${MPV_ROOT}
    PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPV
    REQUIRED_VARS MPV_LIBRARY MPV_INCLUDE_DIR
    VERSION_VAR PC_MPV_VERSION
)

if(MPV_FOUND)
    set(MPV_INCLUDE_DIRS ${MPV_INCLUDE_DIR})
    set(MPV_LIBRARIES ${MPV_LIBRARY})

    if(NOT TARGET MPV::MPV)
        add_library(MPV::MPV UNKNOWN IMPORTED)
        set_target_properties(MPV::MPV PROPERTIES
            IMPORTED_LOCATION "${MPV_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(MPV_INCLUDE_DIR MPV_LIBRARY)
