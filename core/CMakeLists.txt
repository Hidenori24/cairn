# roadmap_core: UI非依存の静的ライブラリ。GUI・Win32・ImGui を include しない（SPEC 3.1）
include(${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake)
set(_gen ${CMAKE_CURRENT_BINARY_DIR}/generated)
roadmap_embed_file(${_gen}/sample_hierarchy.cpp hierarchyCsv ${PROJECT_SOURCE_DIR}/golden/samples/hierarchy_demo.csv)
roadmap_embed_file(${_gen}/sample_leveling.cpp levelingCsv ${PROJECT_SOURCE_DIR}/golden/samples/leveling_demo.csv)
roadmap_embed_file(${_gen}/sample_embedded_sw.cpp embeddedSwCsv ${PROJECT_SOURCE_DIR}/core/samples/embedded_sw.csv)

add_library(roadmap_core STATIC
  src/text.cpp
  src/encoding.cpp
  src/calendar.cpp
  src/model.cpp
  src/csv.cpp
  src/engine.cpp
  src/schedule.cpp
  src/clock.cpp
  src/templates.cpp
  src/filter.cpp
  src/link_candidates.cpp
  src/other_links.cpp
  src/dependency_view.cpp
  src/pred_edit.cpp
  src/commands.cpp
  src/outline.cpp
  src/issue_name.cpp
  src/project_io.cpp
  src/filestore.cpp
  src/store.cpp
  src/attachments.cpp
  src/jira_check.cpp
  src/stats.cpp
  src/gen.cpp
  ${_gen}/sample_hierarchy.cpp
  ${_gen}/sample_leveling.cpp
  ${_gen}/sample_embedded_sw.cpp
)
target_include_directories(roadmap_core PUBLIC include)
target_link_libraries(roadmap_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(roadmap_core PUBLIC cxx_std_20)
roadmap_set_warnings(roadmap_core)
