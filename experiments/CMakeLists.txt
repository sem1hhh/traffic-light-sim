add_library(tsc_experiments STATIC
    stats.cpp
    bench.cpp
)

target_include_directories(tsc_experiments PUBLIC .)
target_link_libraries(tsc_experiments PUBLIC tsc_core tsc_controllers)

target_compile_features(tsc_experiments PUBLIC cxx_std_17)

# --- bench araci ------------------------------------------------------------
add_executable(bench bench_main.cpp)
target_link_libraries(bench PRIVATE tsc_experiments)

set_target_properties(bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/experiments
)

# --- istatistik testleri ---------------------------------------------------
add_executable(run_experiment_tests
    test_main.cpp
    stats_test.cpp
)
target_include_directories(run_experiment_tests PRIVATE ${CMAKE_SOURCE_DIR}/sim-core/tests)
target_link_libraries(run_experiment_tests PRIVATE tsc_experiments third_party)

add_test(NAME experiment_unit_tests COMMAND run_experiment_tests)
