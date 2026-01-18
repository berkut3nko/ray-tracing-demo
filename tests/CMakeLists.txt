add_executable(unit_tests test_main.cpp)
target_link_libraries(unit_tests 
    PRIVATE 
        GTest::gtest_main
)
add_test(NAME CoreTests COMMAND unit_tests)