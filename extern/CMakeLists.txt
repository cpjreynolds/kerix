add_subdirectory(boost-1.90.0 EXCLUDE_FROM_ALL)

set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest
  GIT_TAG v2.5.2 # tag, branch or even commit SHA
)
FetchContent_MakeAvailable(doctest)
