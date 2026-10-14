add_library(tsc_core STATIC
    src/types.cpp
    src/rng.cpp
    src/grid.cpp
)

target_include_directories(tsc_core PUBLIC include)

target_compile_features(tsc_core PUBLIC cxx_std_17)

add_subdirectory(tests)