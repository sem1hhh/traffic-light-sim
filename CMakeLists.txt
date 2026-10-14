cmake_minimum_required(VERSION 3.20)

project(SimTest
    VERSION 0.1.0
    DESCRIPTION "Trafik isigi kontrolcusu simulasyonu"
    LANGUAGES CXX
)

# --- Global derleme ayarlari ------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Uyarilari hataya cevir: derleme temizligi, sessiz hata birikimini onler.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# --- Alt dizinler -----------------------------------------------------------
# enable_testing() add_subdirectory ONCESI cagrilmalidir; aksi halde alt
# dizinlerde kaydedilen add_test() cagrilari gorunmez.
enable_testing()

add_subdirectory(third_party)
add_subdirectory(sim-core)