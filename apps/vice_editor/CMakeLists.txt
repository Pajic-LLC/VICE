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
  vice_editor_asset_index
  STATIC
    src/asset_index.cpp
    src/mesh_picking.cpp
)

add_library(VICE::EditorAssetIndex ALIAS vice_editor_asset_index)

target_include_directories(
  vice_editor_asset_index
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_editor_asset_index
  PUBLIC
    VICE::Math
    VICE::Render
    VICE::RigidBody
    VICE::World
)
target_compile_features(vice_editor_asset_index PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_asset_index)

add_library(
  vice_editor_profiler
  STATIC
    src/profiler.cpp
)

add_library(VICE::EditorProfiler ALIAS vice_editor_profiler)

target_include_directories(
  vice_editor_profiler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_editor_profiler
  PUBLIC
    VICE::Jobs
    VICE::Render
    VICE::Simulation
    VICE::World
)
target_compile_features(vice_editor_profiler PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_profiler)

add_library(
  vice_editor_preview
  STATIC
    src/preview_runtime.cpp
)

add_library(VICE::EditorPreview ALIAS vice_editor_preview)

target_include_directories(
  vice_editor_preview
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_editor_preview
  PUBLIC
    VICE::EditorAssetIndex
    VICE::Jobs
    VICE::RigidBody
    VICE::RigidBodyWorld
    VICE::Simulation
    VICE::SimulationWorld
    VICE::World
)
target_compile_features(vice_editor_preview PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_preview)

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
    src/physics_debug_ui.cpp
    src/profiler_ui.cpp
    src/resource_browser_ui.cpp
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
    VICE::EditorAssetIndex
    VICE::EditorPreview
    VICE::EditorProfiler
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
  src/physics_debug_renderer.cpp
  src/resource_mesh_renderer.cpp
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
    VICE::EditorAssetIndex
    VICE::EditorPreview
    VICE::EditorProfiler
    VICE::EditorState
    VICE::EditorUi
)
target_compile_features(vice_editor PRIVATE cxx_std_23)
target_compile_definitions(
  vice_editor
  PRIVATE
    VICE_EDITOR_LAYOUT_FILE="${CMAKE_BINARY_DIR}/editor/vice_editor.ini"
    VICE_EDITOR_SAVE_FILE="${CMAKE_BINARY_DIR}/editor/vice_editor_world.vworldc"
    VICE_EDITOR_ASSET_SOURCE_ROOT="${PROJECT_SOURCE_DIR}/assets"
    VICE_EDITOR_ASSET_COOKED_ROOT="${CMAKE_BINARY_DIR}/editor_assets"
)
vice_enable_warnings(vice_editor)

if(VICE_BUILD_TOOLS)
  set(_vice_editor_asset_outputs)

  if(TARGET vice_world_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_world_sources "${PROJECT_SOURCE_DIR}/assets/worlds/*.vworld")
    foreach(_source IN LISTS _vice_editor_world_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${CMAKE_BINARY_DIR}/editor_assets/worlds/${_name}.vworldc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/editor_assets/worlds"
        COMMAND $<TARGET_FILE:vice_world_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_world_cooker "${_source}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
      if(_name STREQUAL "vice_lab")
        set(_vice_editor_world_asset "${_output}")
      endif()
    endforeach()
  endif()

  if(TARGET vice_mesh_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_mesh_sources "${PROJECT_SOURCE_DIR}/assets/meshes/*.vmesh")
    foreach(_source IN LISTS _vice_editor_mesh_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${CMAKE_BINARY_DIR}/editor_assets/meshes/${_name}.vmeshc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/editor_assets/meshes"
        COMMAND $<TARGET_FILE:vice_mesh_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_mesh_cooker "${_source}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
    endforeach()
  endif()

  if(TARGET vice_collision_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_collision_sources "${PROJECT_SOURCE_DIR}/assets/collision/*.vcollision")
    foreach(_source IN LISTS _vice_editor_collision_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${CMAKE_BINARY_DIR}/editor_assets/collision/${_name}.vcollisionc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/editor_assets/collision"
        COMMAND $<TARGET_FILE:vice_collision_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_collision_cooker "${_source}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
    endforeach()
  endif()

  if(TARGET vice_rigid_material_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_material_sources "${PROJECT_SOURCE_DIR}/assets/materials/*.vrigmat")
    foreach(_source IN LISTS _vice_editor_material_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${CMAKE_BINARY_DIR}/editor_assets/materials/${_name}.vrigmatc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/editor_assets/materials"
        COMMAND $<TARGET_FILE:vice_rigid_material_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_rigid_material_cooker "${_source}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
    endforeach()
  endif()

  if(_vice_editor_asset_outputs)
    add_custom_target(vice_editor_asset_cache DEPENDS ${_vice_editor_asset_outputs})
    add_dependencies(vice_editor vice_editor_asset_cache)
  endif()

  if(DEFINED _vice_editor_world_asset)
    target_compile_definitions(
      vice_editor
      PRIVATE
        VICE_EDITOR_COOKED_WORLD="${_vice_editor_world_asset}"
    )
  endif()
endif()
