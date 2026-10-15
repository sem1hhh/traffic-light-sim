add_executable(run_tests
    test_main.cpp
    test_types.cpp
    test_rng.cpp
    test_grid.cpp
    test_network.cpp
    test_world.cpp
    test_intersection.cpp
    test_car_following.cpp
)

target_link_libraries(run_tests PRIVATE tsc_core third_party)

# Cikti yolunu her yerde ayni tut (build/tests/run_tests).
set_target_properties(run_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_test(NAME sim_core_unit_tests COMMAND run_tests)