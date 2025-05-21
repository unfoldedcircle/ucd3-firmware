file(GLOB SRCS *.cpp)
file(GLOB MOCK_SRCS ../mocks/*.c)

enable_testing()

add_executable(
  improv_wifi
  ${SRCS}
  ${MOCK_SRCS}
  ../../components/improv_wifi/improv.c
)

target_include_directories(
  improv_wifi
  PRIVATE
  ../mocks
  "${CMAKE_CURRENT_SOURCE_DIR}/../../components/improv_wifi"
)

target_link_libraries(
  improv_wifi
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(improv_wifi)
