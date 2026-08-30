add_executable(alll_tests
  test_async_logger.cpp
  test_spsc_ring_buffer.cpp
  test_mpsc_ring_buffer.cpp
  test_sync_logger.cpp
)
target_link_libraries(alll_tests PRIVATE logger_core GTest::gtest_main)

if(ALLL_ENABLE_TSAN)
  target_compile_options(alll_tests PRIVATE -fsanitize=thread -g)
  target_link_options(alll_tests PRIVATE -fsanitize=thread)
endif()

include(GoogleTest)
gtest_discover_tests(alll_tests)
