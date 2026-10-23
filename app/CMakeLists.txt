# GUI（Dear ImGui）。UI のコード（app/ui）は ImGui だけに依存し、Win32・DX11 は app/platform に閉じ込める。
# Linux では UI をヘッドレスで動かすスモークテスト（ROADMAP_UI_TESTS）だけをビルドする。

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(imgui_lib PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
if(WIN32)
  target_sources(imgui_lib PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui_lib PUBLIC d3d11 d3dcompiler dxgi imm32 dwmapi)
  # IME の候補ウィンドウを入力位置に合わせる（MSVC 以外では既定で無効なので明示する）
  target_compile_definitions(imgui_lib PUBLIC IMGUI_ENABLE_WIN32_DEFAULT_IME_FUNCTIONS)
endif()
if(MSVC)
  target_compile_options(imgui_lib PRIVATE /utf-8)
endif()

add_library(roadmap_ui STATIC
  ui/theme.cpp
  ui/icons.cpp
  ui/widgets.cpp
  ui/app_ui.cpp
  ui/view_list.cpp
  ui/view_gantt.cpp
  ui/view_board.cpp
  ui/link_drag.cpp
  ui/view_goals.cpp
  ui/view_stats.cpp
  ui/view_today.cpp
  ui/panel_detail.cpp
  ui/panel_attach.cpp
  ui/panel_links.cpp
  ui/image_decode.cpp
  ui/palette.cpp
  ui/dialogs.cpp
)
target_include_directories(roadmap_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# stb_image（画像のデコード。単一ヘッダ・パブリックドメイン。third_party/stb に同梱）。警告を出さない SYSTEM 扱い
target_include_directories(roadmap_ui SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
target_link_libraries(roadmap_ui PUBLIC roadmap_controller imgui_lib)
roadmap_set_warnings(roadmap_ui)
target_compile_definitions(roadmap_ui PRIVATE ROADMAP_VERSION="${PROJECT_VERSION}")

if(ROADMAP_BUILD_APP AND WIN32)
  enable_language(RC)
  add_executable(roadmap_app WIN32
    platform/win32_main.cpp
    platform/win32_platform.cpp
    platform/win32_gh.cpp
    platform/win32_lock.cpp
    resources/app.rc
  )
  target_link_libraries(roadmap_app PRIVATE roadmap_ui shell32 ole32 comdlg32 uuid)
  roadmap_set_warnings(roadmap_app)
  target_compile_definitions(roadmap_app PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(MINGW)
    target_link_options(roadmap_app PRIVATE -static -municode $<$<CONFIG:Release>:-s>)
  endif()
  if(MSVC)
    target_link_options(roadmap_app PRIVATE /MANIFEST:NO)  # マニフェストは app.rc に入れている
  endif()
  # 実行ファイル名だけ Cairn（内部のターゲット名 roadmap_app・名前空間 roadmap:: は変えない。Issue #82）
  set_target_properties(roadmap_app PROPERTIES OUTPUT_NAME "Cairn")
endif()

if(ROADMAP_UI_TESTS)
  add_executable(roadmap_ui_smoke tests/ui_smoke.cpp)
  target_link_libraries(roadmap_ui_smoke PRIVATE roadmap_ui)
  target_compile_definitions(roadmap_ui_smoke PRIVATE ROADMAP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  roadmap_set_warnings(roadmap_ui_smoke)
  add_test(NAME ui_smoke COMMAND roadmap_ui_smoke)
endif()

if(ROADMAP_UI_TESTS)
  # 画面の確認用（テストではない。ImGui の描画データをソフトウェアで画像にする）
  add_executable(roadmap_ui_screens tests/ui_screens.cpp)
  target_link_libraries(roadmap_ui_screens PRIVATE roadmap_ui)
  target_compile_definitions(roadmap_ui_screens PRIVATE ROADMAP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  roadmap_set_warnings(roadmap_ui_screens)
endif()
