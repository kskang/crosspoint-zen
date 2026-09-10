add_executable(ReaderOrientationTest
  ReaderOrientationTest.cpp
)

target_link_libraries(ReaderOrientationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderOrientationTest)
