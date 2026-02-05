file(GLOB_RECURSE KERIX_HEADERS CONFIGURE_DEPENDS *.hpp)
file(GLOB_RECURSE KERIX_SOURCES CONFIGURE_DEPENDS *.cpp)
list(FILTER KERIX_SOURCES EXCLUDE REGEX .*\/demo.cpp)

# concrete binary dependencies
set(KERIX_LIBRARIES
    Boost::compat
)

list(APPEND KERIX_DEFINES
    KERIX_DUMMY
)

define_endianness()
define_system()

if(KERIX_CHECK_COPYRIGHT)
    set_copyright_check()
endif()

make_option_defines()

# propagate stuff upwards
list(APPEND PROJECT_LIBRARIES ${KERIX_LIBRARIES})
set(PROJECT_LIBRARIES ${PROJECT_LIBRARIES} PARENT_SCOPE)

add_library(kerix ${KERIX_SOURCES} ${KERIX_HEADERS})

target_compile_features(kerix PUBLIC cxx_std_23 c_std_11)

target_include_directories(kerix PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_definitions(kerix PUBLIC ${KERIX_DEFINES})

target_link_libraries(kerix PUBLIC ${KERIX_LIBRARIES})

if(MSVC)
    target_compile_options(kerix PUBLIC /W4)
else()
    target_compile_options(kerix PUBLIC -Wall -Wextra -fno-char8_t)
endif()

set_compile_commands_copy()

add_executable(demo demo.cpp)
target_link_libraries(demo kerix)

#if(BUILD_TESTING)
#include(${PROJECT_SOURCE_DIR}/extern/doctest-2.4.11/scripts/cmake/doctest.cmake)
#add_executable(testbin ${KERIX_TESTS} ${KERIX_HEADERS})
#target_link_libraries(testbin kerix_common doctest)
#doctest_discover_tests(testbin)
#endif()

add_custom_target(
    run
    COMMAND "$<TARGET_FILE:demo>"
    DEPENDS demo
    VERBATIM)

