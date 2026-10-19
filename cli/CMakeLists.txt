add_executable(rmcli main.cpp)
target_link_libraries(rmcli PRIVATE roadmap_core)
roadmap_set_warnings(rmcli)
if(WIN32)
  target_link_libraries(rmcli PRIVATE shell32)
  # windows.h の min/max マクロが std::max を壊す（MSVC）
  target_compile_definitions(rmcli PRIVATE NOMINMAX)
endif()
if(MSVC)
  # 日本語の出力のため UTF-8 のコードページで動かす
  target_sources(rmcli PRIVATE rmcli.manifest)
endif()
