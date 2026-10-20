# 依存はバージョンを固定する。インストール済み（vcpkg・システム）なら find_package を優先し、
# なければ FetchContent で取得する（CMake 3.24 の FIND_PACKAGE_ARGS）。
# ネットワークのない環境では -DFETCHCONTENT_SOURCE_DIR_<NAME>=<展開済みのソース> で差し替えられる。
include(FetchContent)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS 3.11)
FetchContent_MakeAvailable(nlohmann_json)

if(ROADMAP_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS 2.5)
  FetchContent_MakeAvailable(doctest)
endif()

if(ROADMAP_BUILD_APP OR ROADMAP_UI_TESTS)
  # Dear ImGui は CMake を持たないので、ソースを取ってきて自前でライブラリにする（app/CMakeLists.txt）
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)
endif()
