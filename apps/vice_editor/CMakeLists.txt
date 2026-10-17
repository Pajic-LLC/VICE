include(FetchContent)

FetchContent_Declare(
  DearImGui
  GIT_REPOSITORY "https://github.com/ocornut/imgui.git"
  GIT_TAG "v1.93.0-docking"
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(DearImGui)

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

add_library(
  vice_editor_imgui
  STATIC
    "${dearimgui_SOURCE_DIR}/imgui.cpp"
    "${dearimgui_SOURCE_DIR}/imgui_draw.cpp"
    "${dearimgui_SOURCE_DIR}/imgui_tables.cpp"
    "${dearimgui_SOURCE_DIR}/imgui_widgets.cpp"
)

target_include_directories(
  vice_editor_imgui
  PUBLIC
    "${dearimgui_SOURCE_DIR}"
)
target_compile_features(vice_editor_imgui PUBLIC cxx_std_23)
set_target_properties(vice_editor_imgui PROPERTIES FOLDER "Third Party")

add_library(
  vice_editor_ui
  STATIC
    src/ui.cpp
    src/ui_theme.cpp
)

add_library(VICE::EditorUi ALIAS vice_editor_ui)

target_include_directories(
  vice_editor_ui
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    "${dearimgui_SOURCE_DIR}"
)

target_link_libraries(
  vice_editor_ui
  PUBLIC
    VICE::EditorState
    VICE::Platform
  PRIVATE
    vice_editor_imgui
)
target_compile_features(vice_editor_ui PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_ui)
