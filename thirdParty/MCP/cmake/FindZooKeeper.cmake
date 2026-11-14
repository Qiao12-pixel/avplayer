# FindZooKeeper.cmake
# Finds the ZooKeeper C client library (multi-threaded).
#
# Exported targets:
#   ZooKeeper::zookeeper_mt
#
# Exported variables:
#   ZooKeeper_FOUND
#   ZooKeeper_INCLUDE_DIRS
#   ZooKeeper_LIBRARIES

find_path(ZooKeeper_INCLUDE_DIR
    NAMES zookeeper/zookeeper.h
    PATHS /opt/homebrew/include /usr/local/include /usr/include
)

find_library(ZooKeeper_LIBRARY
    NAMES zookeeper_mt
    PATHS /opt/homebrew/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZooKeeper
    REQUIRED_VARS ZooKeeper_LIBRARY ZooKeeper_INCLUDE_DIR
)

if(ZooKeeper_FOUND)
    set(ZooKeeper_INCLUDE_DIRS "${ZooKeeper_INCLUDE_DIR}")
    set(ZooKeeper_LIBRARIES "${ZooKeeper_LIBRARY}")

    if(NOT TARGET ZooKeeper::zookeeper_mt)
        add_library(ZooKeeper::zookeeper_mt UNKNOWN IMPORTED)
        set_target_properties(ZooKeeper::zookeeper_mt PROPERTIES
            IMPORTED_LOCATION "${ZooKeeper_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ZooKeeper_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(ZooKeeper_INCLUDE_DIR ZooKeeper_LIBRARY)
