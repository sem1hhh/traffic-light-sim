add_library(tsc_types STATIC src/types.cpp)

target_include_directories(tsc_types PUBLIC include)

target_compile_features(tsc_types PUBLIC cxx_std_17)

add_subdirectory(tests)