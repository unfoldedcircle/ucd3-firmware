file(GLOB SRCS *.cpp)

enable_testing()

add_executable(
  common
  ${SRCS}
  ../../components/common/string_util.cpp
)

target_include_directories(
  common
  PRIVATE
  ../mocks
  "${CMAKE_CURRENT_SOURCE_DIR}/../../components/common"
)

target_link_libraries(
  common
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(common)
