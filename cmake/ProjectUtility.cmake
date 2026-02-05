# my own cmake utility module I use between various projects

string(TOLOWER ${CMAKE_PROJECT_NAME} PROJECT_LOWER)
string(TOUPPER ${CMAKE_PROJECT_NAME} PROJECT_UPPER)

set(PROJECT_OPTIONS ${PROJECT_UPPER}_OPTIONS)
set(PROJECT_DEFINES ${PROJECT_UPPER}_DEFINES)
set(PROJECT_TOOLS_DIR "${PROJECT_SOURCE_DIR}/tools")

# set(PROJECT_SOURCES ${PROJECT_UPPER}_SOURCES)
# set(PROJECT_HEADERS ${PROJECT_UPPER}_HEADERS)

function(${PROJECT_LOWER}_option name desc value)
    string(CONCAT opt_desc "enable " ${desc})
    option(${PROJECT_UPPER}_${name} ${opt_desc} ${value})
    add_feature_info(${name} ${value} ${desc})
    list(APPEND ${PROJECT_UPPER}_OPTIONS ${PROJECT_UPPER}_${name})
    return(PROPAGATE ${PROJECT_UPPER}_OPTIONS)
endfunction()

function(make_option_defines)
    foreach(opt ${${PROJECT_OPTIONS}})
        list(APPEND ${PROJECT_DEFINES} ${opt}=$<BOOL:${${opt}}>)
    endforeach()
    return(PROPAGATE ${PROJECT_DEFINES})
endfunction()

option(${PROJECT_UPPER}_DELETE_DS_STORE "delete .DS_Store files" ${APPLE})

function(set_copyright_check)
    find_package(Python COMPONENTS Interpreter REQUIRED)
    add_custom_target(check_copyright ALL
        COMMAND ${Python_EXECUTABLE} ${PROJECT_TOOLS_DIR}/add_copyright.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "check copyright"
    )

    add_custom_target(add_copyright
        COMMAND ${Python_EXECUTABLE} ${PROJECT_TOOLS_DIR}/add_copyright.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "add copyright"
    )
endfunction()

if(${PROJECT_UPPER}_DELETE_DS_STORE)
    add_custom_target(delete_ds_store ALL
        COMMAND find ${PROJECT_SOURCE_DIR} -name ".DS_Store" -delete
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "deleting .DS_Store"
        VERBATIM
    )
endif()

function(define_endianness)
    if (${CMAKE_CXX_BYTE_ORDER} STREQUAL "BIG_ENDIAN")
        list(APPEND ${PROJECT_DEFINES} ${PROJECT_UPPER}_BIG_ENDIAN)
    elseif(${CMAKE_CXX_BYTE_ORDER} STREQUAL "LITTLE_ENDIAN")
        list(APPEND ${PROJECT_DEFINES} ${PROJECT_UPPER}_LITTLE_ENDIAN)
    endif()
    return(PROPAGATE ${PROJECT_DEFINES})
endfunction()

# easier than relying on compiler-specific defines
function(define_system)
    if(WIN32)
        list(APPEND ${PROJECT_DEFINES} WINDOWS)
    endif()
    if(UNIX)
        list(APPEND ${PROJECT_DEFINES} UNIX) # includes macos and cygwin
    endif()
    if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
        list(APPEND ${PROJECT_DEFINES} DARWIN)
    endif()
    return(PROPAGATE ${PROJECT_DEFINES})
endfunction()

function(set_default_build)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        message(STATUS "Build type not set, default to Debug")
        set(CMAKE_BUILD_TYPE Debug CACHE
            STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
    endif()
endfunction()

function(set_compile_commands_copy)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                ${PROJECT_BINARY_DIR}/compile_commands.json
                ${PROJECT_SOURCE_DIR}/compile_commands.json
        )
    endif()
endfunction()

