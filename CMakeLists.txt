cmake_minimum_required(VERSION 3.24)
project(ProjectRoadmap VERSION 1.1.0 LANGUAGES CXX)

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
option(ROADMAP_UI_TESTS "UI（ImGui）をヘッドレスで動かすスモークテストをビルドする（Linux可）" OFF)
option(ROADMAP_WERROR "警告をエラーにする" OFF)
option(ROADMAP_SANITIZE "ASan + UBSan（GCC/Clang）" OFF)

if(MINGW)
  # MinGW でも追加のDLLなしで動くように
  add_link_options(-static)
endif()
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
  if(UNIX)
    # リリースノートを CHANGELOG から作るスクリプト（「## 1.0.0-rc.1」のような見出しでも別の節を拾わない）
    add_test(NAME release_notes COMMAND sh ${PROJECT_SOURCE_DIR}/tests/release_notes_test.sh ${PROJECT_SOURCE_DIR}/tools/release_notes.sh)
  endif()
endif()

# ポータブル版のフォルダ構成（cmake --install <build> --component portable --prefix <出力先>）
# exe の隣に data\projects\（保存先）・samples\・README.txt を置く。依存ライブラリの install は含めない
if(WIN32 AND ROADMAP_BUILD_APP)
  install(TARGETS roadmap_app rmcli RUNTIME DESTINATION . COMPONENT portable)
  install(DIRECTORY packaging/portable/ DESTINATION . COMPONENT portable)
  install(FILES golden/samples/hierarchy_demo.csv golden/samples/leveling_demo.csv
          DESTINATION samples COMPONENT portable)
  install(FILES THIRD_PARTY.md DESTINATION . COMPONENT portable)
endif()
