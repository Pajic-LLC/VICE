add_library(
  vice_editor_state
  STATIC
    src/state.cpp
)

add_library(VICE::EditorState ALIAS vice_editor_state)

target_include_directories(
  vice_editor_state
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_editor_state
  PUBLIC
    VICE::Core
    VICE::World
)
target_compile_features(vice_editor_state PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_state)
