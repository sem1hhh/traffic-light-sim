add_executable(run_tests test_main.cpp test_types.cpp)

target_link_libraries(run_tests PRIVATE tsc_types third_party)

# Cikti yolunu her yerde ayni tut sabit tut.
set_target_properties(run_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_test(NAME sim_core_unit_tests COMMAND run_tests)