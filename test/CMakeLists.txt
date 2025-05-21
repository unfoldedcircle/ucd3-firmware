cmake_minimum_required(VERSION 3.14)
project(unit_tests)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# TODO individual test binaries work, but is there a better way with CMake to
#      put all subdirectory tests into the same binary?
add_subdirectory(common)
add_subdirectory(infrared)
add_subdirectory(improv_wifi)
add_subdirectory(preferences)
# add_subdirectory(foobar)
