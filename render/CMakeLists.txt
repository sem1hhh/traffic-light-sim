# render/ — GlistEngine arayuz katmani (Faz 7)
#
# AYRI bir CMake projesidir: sim-core, controllers ve experiments'tan BAGIMSIZDIR.
# Neden ayri: GlistEngine her makinada kurulu degil. Ana build (run_tests,
# bench, train) motor olmadan da calismalidir (AGENTS.md: CI-safe, grafik yok).
# Derlemek icin:
#   cmake -S render -B render/build -DGLIST_ENGINE_DIR=~/dev/glist/GlistEngine
#   cmake --build render/build
# Motor varsayilan olarak ~/dev/glist/GlistEngine'de aranir.

cmake_minimum_required(VERSION 3.20)

project(tsc_render
    VERSION 0.1.0
    DESCRIPTION "Trafik simulasyonu render katmani (GlistEngine)"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Motor yolu --------------------------------------------------------------
# GlistEngine kurulumu bagimsiz dizindedir (installer: ~/dev/glist/GlistEngine).
set(GLIST_ENGINE_DIR "$ENV{HOME}/dev/glist/GlistEngine" CACHE PATH
    "GlistEngine repo kok dizini (icinde engine/ altinda CMakeLists.txt olmali)")

if(NOT EXISTS "${GLIST_ENGINE_DIR}/engine/CMakeLists.txt")
    message(FATAL_ERROR
        "GlistEngine bulunamadi: ${GLIST_ENGINE_DIR}/engine/CMakeLists.txt yok.\n"
        "Kurulum: https://github.com/GlistEngine/GlistEngine (macOS kurulum betigi "
        "InstallScripts deposunda). Veya -DGLIST_ENGINE_DIR=<yol> ile verin.")
endif()

# --- macOS brew yollari ------------------------------------------------------
# Motorun CMake'i bu degiskenleri add_subdirectory ONCESI ister (ornek
# samples/gisImage ile ayni kalip). arm64 = /opt/homebrew.
if(APPLE)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE TSC_DEVICE_ARCH
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT ${TSC_DEVICE_ARCH} STREQUAL "arm64")
        set(SYS_NAME "macos-x86_64")
        set(SYS_PATH "/usr/local")
    else()
        set(SYS_NAME "macos")
        set(SYS_PATH "/opt/homebrew")
    endif()
    set(BREW_PATH "${SYS_PATH}/opt")
    set(SYS_INC_PATH "${SYS_PATH}/include")
    set(ENGINE_USE_SYMLINKS ON)
endif()

# --- Motoru ekle -------------------------------------------------------------
# APPLE'da add_subdirectory, diger platformlarda include (motor CMake'i
# iki yolu da destekliyor; samples/gisImage kalibi).
add_subdirectory(${GLIST_ENGINE_DIR}/engine ./_build_engine)

# --- Uygulamamiz -------------------------------------------------------------
# Sim-core + controllers kaynaklari iki hedefte de kullanilir (uygulama ve
# GUI'siz olcum araci). Tek listede tutulur ki ikisi AYNI cekirdegi derlesin.
set(TSC_SIM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/rng.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/grid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/network.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/intersection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/src/car_following.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/fixed_time.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/threshold.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers/q_learning.cpp
)

add_executable(tsc_render
    src/main.cpp
    src/render_app.cpp
    src/render_canvas.cpp
    src/sim_scene.cpp
    src/scene3d.cpp

    # sim-core ve controllers kaynaklari DOGRUDAN buraya derlenir (kutuphane
    # olarak add_subdirectory DEGIL). Sebep: sim-core/CMakeLists.txt test
    # hedeflerini (doctest/third_party) de cagiriyor; render ayri bir proje ve
    # motor + simulasyonu tek basina derleyebilmeli. Ayrica kural: o dizinlere
    # DOKUNULMAZ. Kaynak dosyalar oldugu gibi okunur, hicbiri degistirilmez.
    ${TSC_SIM_SOURCES}
)

# --- GUI'siz ofset olcumu ----------------------------------------------------
# Cizimle AYNI geometriyi (SimScene + coords.h) kullanir; OpenGL GEREKTIRMEZ.
# "Dikey kol ofseti" iddiasini pencere acmadan tekrarlanabilir sekilde olcer.
add_executable(tsc_measure_offset
    tools/measure_offset.cpp
    src/sim_scene.cpp
    ${TSC_SIM_SOURCES}
)

# sim-core ve controllers basliklari.
target_include_directories(tsc_render PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers
)

target_include_directories(tsc_measure_offset PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../sim-core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers
)

# Motor include yollari PUBLIC verildigi icin sadece baglanti yeterli.
target_link_libraries(tsc_render PRIVATE GlistEngine)

# Kendi kodumuz temiz olsun: -Wall -Wextra. Motor basliklari warning-free
# degil (onlari derleyemeyiz, motorun kendi kodu), bu yuzden -Werror YOK;
# yalnizca kendi kaynak dosyalarimiza uygulanir.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        src/main.cpp src/render_app.cpp src/render_canvas.cpp src/sim_scene.cpp
        tools/measure_offset.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra"
    )
endif()
