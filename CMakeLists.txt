cmake_minimum_required (VERSION 3.28)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

project(
    kerix
    VERSION 0.0.1
    DESCRIPTION "a signals & slots library"
    LANGUAGES C CXX
)

# project-wide includes
include(GNUInstallDirs)
# feature printing
include(FeatureSummary)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectUtility)

set_default_build()

# build process options
option(KERIX_CHECK_COPYRIGHT "check source file copyright notices" ON)

# build options
kerix_option(DEBUG "debug features" ON)
kerix_option(TEST "build tests" OFF)

set(KERIX_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools")

# youcompleteme/clangd support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BUILD_TESTING ${KERIX_TEST})

# global compiler options (for codegen flags)

if(MSVC)
    # placeholder
else()
    add_compile_options(-march=native)
endif()

add_subdirectory(extern)

# ==[project]==
add_subdirectory(kerix)

feature_summary(WHAT ENABLED_FEATURES DISABLED_FEATURES)

