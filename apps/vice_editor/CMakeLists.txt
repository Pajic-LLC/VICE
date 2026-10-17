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
    src/ui_renderer.cpp
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
    VICE::Render
  PRIVATE
    vice_editor_imgui
)
target_compile_features(vice_editor_ui PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_ui)

add_executable(
  vice_editor
  app.cpp
  src/scene_renderer.cpp
)

target_include_directories(
  vice_editor
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
  vice_editor
  PRIVATE
    VICE::Core
    VICE::Math
    VICE::Jobs
    VICE::Platform
    VICE::Render
    VICE::World
    VICE::EditorState
    VICE::EditorUi
)
target_compile_features(vice_editor PRIVATE cxx_std_23)
target_compile_definitions(
  vice_editor
  PRIVATE
    VICE_EDITOR_LAYOUT_FILE="${CMAKE_BINARY_DIR}/editor/vice_editor.ini"
)
vice_enable_warnings(vice_editor)

if(VICE_BUILD_TOOLS)
  if(TARGET vice_world_cooker)
    set(_vice_editor_world_source "${PROJECT_SOURCE_DIR}/assets/worlds/vice_lab.vworld")
    set(_vice_editor_world_asset "${CMAKE_BINARY_DIR}/cooked/vice_editor_world.vworldc")

    add_custom_command(
      OUTPUT "${_vice_editor_world_asset}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/cooked"
      COMMAND
        $<TARGET_FILE:vice_world_cooker>
        --input "${_vice_editor_world_source}"
        --output "${_vice_editor_world_asset}"
      DEPENDS
        vice_world_cooker
        "${_vice_editor_world_source}"
      VERBATIM
    )

    add_custom_target(
      vice_editor_world_asset
      DEPENDS "${_vice_editor_world_asset}"
    )
    add_dependencies(vice_editor vice_editor_world_asset)
    target_compile_definitions(
      vice_editor
      PRIVATE
        VICE_EDITOR_COOKED_WORLD="${_vice_editor_world_asset}"
    )
  endif()

  if(TARGET vice_mesh_cooker)
    set(_vice_editor_mesh_source "${PROJECT_SOURCE_DIR}/assets/meshes/vice_lab_ramp.vmesh")
    set(_vice_editor_mesh_asset "${CMAKE_BINARY_DIR}/cooked/vice_editor_scene.vmeshc")

    add_custom_command(
      OUTPUT "${_vice_editor_mesh_asset}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/cooked"
      COMMAND
        $<TARGET_FILE:vice_mesh_cooker>
        --input "${_vice_editor_mesh_source}"
        --output "${_vice_editor_mesh_asset}"
      DEPENDS
        vice_mesh_cooker
        "${_vice_editor_mesh_source}"
      VERBATIM
    )

    add_custom_target(
      vice_editor_scene_asset
      DEPENDS "${_vice_editor_mesh_asset}"
    )
    add_dependencies(vice_editor vice_editor_scene_asset)
    target_compile_definitions(
      vice_editor
      PRIVATE
        VICE_EDITOR_COOKED_SCENE_MESH="${_vice_editor_mesh_asset}"
    )
  endif()
endif()
