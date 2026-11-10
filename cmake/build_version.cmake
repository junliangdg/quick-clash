# Refresh at build time so switching commits or editing an existing checkout
# cannot leave a configure-time version behind. Unchanged content keeps its mtime.
add_custom_target(quickclash_build_info
    COMMAND "${CMAKE_COMMAND}"
        "-DQUICKCLASH_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DQUICKCLASH_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DQUICKCLASH_BASE_VERSION=${PROJECT_VERSION}"
        -P "${CMAKE_CURRENT_LIST_DIR}/write_build_info.cmake"
    BYPRODUCTS
        "${CMAKE_CURRENT_BINARY_DIR}/generated/quickclash_build_info.h"
        "${CMAKE_CURRENT_BINARY_DIR}/quickclash_build_info.json"
    COMMENT "Refreshing QuickClash build identity"
    VERBATIM)
