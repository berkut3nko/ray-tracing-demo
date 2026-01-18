add_executable(unit_tests test_main.cpp)

target_compile_features(unit_tests PUBLIC cxx_std_20)

# Scan for modules in test file
set_target_properties(unit_tests PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_link_libraries(unit_tests 
    PRIVATE 
        GTest::gtest_main
        RayTracingCore
)

include(GoogleTest)
gtest_discover_tests(unit_tests)