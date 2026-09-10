add_executable(ReadingPaceTest
  ReadingPaceTest.cpp
)

target_link_libraries(ReadingPaceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingPaceTest)
