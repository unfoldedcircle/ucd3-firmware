file(GLOB SRCS *.cpp)

enable_testing()

add_executable(
  preferences
  ${SRCS}
  ../../components/preferences/uart_config.cpp
)

target_include_directories(
  preferences
  PRIVATE
  ../mocks
  "${CMAKE_CURRENT_SOURCE_DIR}/../../components/preferences"
)

target_link_libraries(
  preferences
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(preferences)
