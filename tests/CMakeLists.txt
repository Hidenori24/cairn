# doctest のテスト。core と cli だけを使う（Linux でも動く）
add_library(roadmap_test_main STATIC test_main.cpp)
target_link_libraries(roadmap_test_main PUBLIC doctest::doctest)
# doctest が std::tuple などを自前で前方宣言しないようにする（MSVC 19.5x の C5285）
target_compile_definitions(roadmap_test_main PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)

add_executable(roadmap_unit_tests
  unit/test_text.cpp
  unit/test_calendar.cpp
  unit/test_csv.cpp
  unit/test_engine.cpp
  unit/test_commands.cpp
  unit/test_outline.cpp
  unit/test_store.cpp
  unit/test_filter.cpp
  unit/test_property.cpp
  unit/test_jira_check.cpp
  unit/test_controller.cpp
)
target_link_libraries(roadmap_unit_tests PRIVATE roadmap_core roadmap_controller roadmap_test_main)
target_compile_definitions(roadmap_unit_tests PRIVATE ROADMAP_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/golden")
roadmap_set_warnings(roadmap_unit_tests)
add_test(NAME unit COMMAND roadmap_unit_tests)

add_executable(roadmap_golden_tests golden/test_golden.cpp)
target_link_libraries(roadmap_golden_tests PRIVATE roadmap_core roadmap_test_main)
target_compile_definitions(roadmap_golden_tests PRIVATE ROADMAP_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/golden")
roadmap_set_warnings(roadmap_golden_tests)
add_test(NAME golden COMMAND roadmap_golden_tests)

# rmcli の出力が golden と1バイトも違わないこと
function(roadmap_cli_golden name expect)
  add_test(NAME cli_golden_${name}
    COMMAND ${CMAKE_COMMAND} -DRMCLI=$<TARGET_FILE:rmcli> -DGOLDEN=${PROJECT_SOURCE_DIR}/golden
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/cli_${name}.out -DEXPECT=${expect} "-DARGS=${ARGN}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/golden/cli_compare.cmake)
endfunction()
foreach(s regulation_roadmap hierarchy_demo leveling_demo)
  roadmap_cli_golden(${s}_base ${s}.base.json schedule samples/${s}.csv --base 2026-10-19)
  roadmap_cli_golden(${s}_level1 ${s}.level1.json schedule samples/${s}.csv --base 2026-10-19 --level 1)
  roadmap_cli_golden(${s}_level2 ${s}.level2.json schedule samples/${s}.csv --base 2026-10-19 --level 2)
  roadmap_cli_golden(${s}_base_1102 ${s}.base_1102.json schedule samples/${s}.csv --base 2026-11-02)
  roadmap_cli_golden(${s}_roundtrip ${s}.roundtrip.csv import-export samples/${s}.csv)
  roadmap_cli_golden(${s}_jira ${s}.jira.csv import-export samples/${s}.csv --format jira --base 2026-10-19)
endforeach()
roadmap_cli_golden(regulation_roadmap_scn_nb regulation_roadmap.scn_nb.json schedule samples/regulation_roadmap.csv --base 2026-10-19 --scenario "第三者(NB)")
add_test(NAME cli_check_jira COMMAND rmcli check-jira --raw ${PROJECT_SOURCE_DIR}/golden/regulation_roadmap.jira.csv
  ${PROJECT_SOURCE_DIR}/golden/hierarchy_demo.jira.csv ${PROJECT_SOURCE_DIR}/golden/leveling_demo.jira.csv)
add_test(NAME cli_check_jira_samples COMMAND rmcli check-jira --base 2026-10-19 ${PROJECT_SOURCE_DIR}/golden/samples/regulation_roadmap.csv
  ${PROJECT_SOURCE_DIR}/golden/samples/hierarchy_demo.csv ${PROJECT_SOURCE_DIR}/golden/samples/leveling_demo.csv)
add_test(NAME cli_help COMMAND rmcli --help)
add_test(NAME cli_bad_input COMMAND rmcli validate ${PROJECT_SOURCE_DIR}/README.md)
set_tests_properties(cli_bad_input PROPERTIES WILL_FAIL TRUE)
