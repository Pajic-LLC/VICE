add_library(
  vice_editor_authored
  STATIC
    ../src/authored_world_document.cpp
    ../src/project_settings.cpp
)

add_library(VICE::EditorAuthored ALIAS vice_editor_authored)

target_include_directories(
  vice_editor_authored
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)

target_link_libraries(
  vice_editor_authored
  PUBLIC
    VICE::EditorState
    VICE::Project
    VICE::WorldCooker
)
target_compile_features(vice_editor_authored PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_authored)

target_link_libraries(vice_editor PRIVATE VICE::EditorAuthored)
