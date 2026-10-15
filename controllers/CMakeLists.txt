# Kontrolcüler sim-core'a BAĞIMLIDIR (arayüz orada tanimli).
add_library(tsc_controllers STATIC
    controller.cpp
    fixed_time.cpp
    threshold.cpp
    q_learning.cpp
)

target_include_directories(tsc_controllers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tsc_controllers PUBLIC tsc_core)

target_compile_features(tsc_controllers PUBLIC cxx_std_17)

# --- kontrolcu testleri ----------------------------------------------------
add_executable(run_controller_tests
    tests/test_main.cpp
    tests/test_controllers.cpp
    tests/test_q_learning.cpp
)
target_include_directories(run_controller_tests PRIVATE ${CMAKE_SOURCE_DIR}/sim-core/tests)
target_link_libraries(run_controller_tests PRIVATE tsc_controllers third_party)

add_test(NAME controller_unit_tests COMMAND run_controller_tests)
