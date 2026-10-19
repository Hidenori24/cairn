cmake_minimum_required(VERSION 3.24)
project(ProjectRoadmap VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
  set(_roadmap_app_default ON)
else()
  set(_roadmap_app_default OFF)
endif()
option(ROADMAP_BUILD_APP "GUI（roadmap_app）をビルドする。Windows のみ" ${_roadmap_app_default})
option(ROADMAP_BUILD_TESTS "core・cli のテストをビルドする" ON)
option(ROADMAP_UI_TESTS "UI（ImGui）をヘッドレスで動かす煙テストをビルドする（Linux可）" OFF)
option(ROADMAP_WERROR "警告をエラーにする" OFF)
option(ROADMAP_SANITIZE "ASan + UBSan（GCC/Clang）" OFF)

if(MSVC)
  # 単一exe（静的CRT）
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(ROADMAP_BUILD_TESTS OR ROADMAP_UI_TESTS)
  enable_testing()
endif()

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(app/controller)

if(ROADMAP_BUILD_APP OR ROADMAP_UI_TESTS)
  add_subdirectory(app)
endif()

if(ROADMAP_BUILD_TESTS)
  add_subdirectory(tests)
endif()
