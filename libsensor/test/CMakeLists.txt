find_package(GTest REQUIRED)

add_executable(test_libsensor_unit
    unit/test_sensor_classification.cpp)
target_link_libraries(test_libsensor_unit PRIVATE
    sensor
    GTest::gtest_main
    graphx_project_options)

add_test(NAME libsensor_unit COMMAND test_libsensor_unit)
set_tests_properties(libsensor_unit PROPERTIES
    LABELS "sensor;unit;ci-safe;host-any")
