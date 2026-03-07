find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(hybrid_assets_tests
  assets/AssetsMockTest.cpp
)

target_link_libraries(hybrid_assets_tests PRIVATE
  GTest::gtest_main
  GTest::gmock
  hybrid::engine
)

target_include_directories(hybrid_assets_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

gtest_discover_tests(hybrid_assets_tests)
