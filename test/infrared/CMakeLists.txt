file(GLOB SRCS *.cpp)

enable_testing()

add_executable(
  infrared
  ${SRCS}
  ../../components/infrared/ir_codes.cpp
  ../../components/infrared/globalcache.cpp
)

target_include_directories(
  infrared
  PRIVATE
  ../mocks
  "${CMAKE_CURRENT_SOURCE_DIR}/../../components/infrared"
)

target_link_libraries(
  infrared
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(infrared)
