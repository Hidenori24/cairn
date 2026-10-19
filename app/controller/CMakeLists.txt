# AppController（UI非依存。Linux でもビルド・テストする）
add_library(roadmap_controller STATIC app_controller.cpp)
target_include_directories(roadmap_controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(roadmap_controller PUBLIC roadmap_core)
roadmap_set_warnings(roadmap_controller)
