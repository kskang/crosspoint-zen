add_executable(PageDensityTest
  PageDensityTest.cpp
)

target_link_libraries(PageDensityTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PageDensityTest)
