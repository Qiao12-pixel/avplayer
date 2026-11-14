# FindHiredis.cmake
# Finds the hiredis C client library.
#
# Exported targets:
#   Hiredis::hiredis
#
# Exported variables:
#   Hiredis_FOUND
#   Hiredis_INCLUDE_DIRS
#   Hiredis_LIBRARIES

find_path(Hiredis_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    PATHS /opt/homebrew/include /usr/local/include /usr/include
)

find_library(Hiredis_LIBRARY
    NAMES hiredis
    PATHS /opt/homebrew/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Hiredis
    REQUIRED_VARS Hiredis_LIBRARY Hiredis_INCLUDE_DIR
)

if(Hiredis_FOUND)
    set(Hiredis_INCLUDE_DIRS "${Hiredis_INCLUDE_DIR}")
    set(Hiredis_LIBRARIES "${Hiredis_LIBRARY}")

    if(NOT TARGET Hiredis::hiredis)
        add_library(Hiredis::hiredis UNKNOWN IMPORTED)
        set_target_properties(Hiredis::hiredis PROPERTIES
            IMPORTED_LOCATION "${Hiredis_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Hiredis_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(Hiredis_INCLUDE_DIR Hiredis_LIBRARY)
