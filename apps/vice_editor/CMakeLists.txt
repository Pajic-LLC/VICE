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
    src/project_session.cpp
    src/project_settings.cpp
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
    VICE::Project
    VICE::World
)
target_compile_features(vice_editor_state PUBLIC cxx_std_23)
vice_enable_warnings(vice_editor_state)

if(TARGET vice_world_cooker_lib AND
   TARGET vice_mesh_cooker_lib AND
   TARGET vice_render_material_cooker_lib AND
   TARGET vice_shader_cooker_lib AND
   TARGET vice_texture_cooker_lib AND
   TARGET vice_environment_cooker_lib AND
   TARGET vice_project_workflow)
  add_library(
    vice_editor_project_build
    STATIC
      src/authored_world_document.cpp
      src/project_build.cpp
    src/project_preview.cpp
      src/project_environment_cook.cpp
      src/project_material_cook.cpp
      src/project_mesh_cook.cpp
      src/project_texture_cook.cpp
  )

  add_library(VICE::EditorProjectBuild ALIAS vice_editor_project_build)

  target_include_directories(
    vice_editor_project_build
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )

  target_link_libraries(
    vice_editor_project_build
    PUBLIC
      VICE::EditorState
      VICE::EditorPreview
      VICE::PresentationCooker
      VICE::EnvironmentCooker
      VICE::MeshCooker
      VICE::Platform
      VICE::ProjectWorkflow
      VICE::RenderMaterialCooker
      VICE::Runtime
      VICE::ShaderCooker
      VICE::TextureCooker
      VICE::World
      VICE::WorldCooker
  )
  target_compile_features(vice_editor_project_build PUBLIC cxx_std_23)
  vice_enable_warnings(vice_editor_project_build)
endif()

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
    VICE::Presentation
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
    VICE::Runtime
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
    src/project_settings_ui.cpp
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
    VICE::Project
    VICE::Render
    VICE::World
    VICE::EditorAssetIndex
    VICE::EditorPreview
    VICE::EditorProfiler
    VICE::EditorState
    VICE::EditorUi
)
if(TARGET vice_editor_project_build)
  target_link_libraries(vice_editor PRIVATE VICE::EditorProjectBuild)
endif()
target_compile_features(vice_editor PRIVATE cxx_std_23)
target_compile_definitions(
  vice_editor
  PRIVATE
    VICE_EDITOR_LAYOUT_FILE="${CMAKE_BINARY_DIR}/editor/vice_editor.ini"
    VICE_EDITOR_LEGACY_SAVE_FILE="${CMAKE_BINARY_DIR}/editor/vice_editor_world.vworldc"
    VICE_EDITOR_STATE_ROOT="${CMAKE_BINARY_DIR}/editor/projects"
    VICE_EDITOR_DEFAULT_PROJECT="${PROJECT_SOURCE_DIR}/vice.viceproj"
    VICE_EDITOR_ASSET_CACHE_ROOT="${CMAKE_BINARY_DIR}/editor_assets"
)
if(TARGET vice_player)
  add_dependencies(vice_editor vice_player)
  target_compile_definitions(
    vice_editor
    PRIVATE
      VICE_EDITOR_PLAYER_EXECUTABLE="$<TARGET_FILE:vice_player>"
  )
  if(TARGET vice_editor_project_build)
    add_dependencies(vice_editor_project_build vice_player)
    target_compile_definitions(
      vice_editor_project_build
      PRIVATE
        VICE_EDITOR_RUNTIME_HOST_EXECUTABLE="$<TARGET_FILE:vice_player>"
    )
  endif()
endif()
vice_enable_warnings(vice_editor)

if(VICE_BUILD_TOOLS)
  set(_vice_editor_default_project_file "${PROJECT_SOURCE_DIR}/vice.viceproj")
  file(STRINGS "${_vice_editor_default_project_file}" _vice_editor_project_id_line REGEX "^id [0-9A-Fa-f-]+$")
  file(STRINGS "${_vice_editor_default_project_file}" _vice_editor_asset_root_line REGEX "^assets \"[^\"]+\"$")
  if(NOT _vice_editor_project_id_line OR NOT _vice_editor_asset_root_line)
    message(FATAL_ERROR "VICE default project manifest is missing its id or assets field")
  endif()
  string(REGEX REPLACE "^id +" "" _vice_editor_default_project_id "${_vice_editor_project_id_line}")
  string(REGEX REPLACE "^assets \"([^\"]+)\"$" "\\1" _vice_editor_default_asset_relative "${_vice_editor_asset_root_line}")
  set(_vice_editor_default_asset_source_root "${PROJECT_SOURCE_DIR}/${_vice_editor_default_asset_relative}")
  set(_vice_editor_default_cooked_root "${CMAKE_BINARY_DIR}/editor_assets/${_vice_editor_default_project_id}")

  set(_vice_editor_asset_outputs)

  if(TARGET vice_world_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_world_sources "${_vice_editor_default_asset_source_root}/worlds/*.vworld")
    foreach(_source IN LISTS _vice_editor_world_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${_vice_editor_default_cooked_root}/worlds/${_name}.vworldc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_vice_editor_default_cooked_root}/worlds"
        COMMAND $<TARGET_FILE:vice_world_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_world_cooker "${_source}" "${_vice_editor_default_project_file}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
    endforeach()
  endif()

  if(TARGET vice_mesh_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_mesh_sources "${_vice_editor_default_asset_source_root}/meshes/*.vmesh")
    foreach(_source IN LISTS _vice_editor_mesh_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${_vice_editor_default_cooked_root}/meshes/${_name}.vmeshc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_vice_editor_default_cooked_root}/meshes"
        COMMAND $<TARGET_FILE:vice_mesh_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_mesh_cooker "${_source}" "${_vice_editor_default_project_file}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
    endforeach()
  endif()

  if(TARGET vice_collision_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_collision_sources "${_vice_editor_default_asset_source_root}/collision/*.vcollision")
    foreach(_source IN LISTS _vice_editor_collision_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${_vice_editor_default_cooked_root}/collision/${_name}.vcollisionc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_vice_editor_default_cooked_root}/collision"
        COMMAND $<TARGET_FILE:vice_collision_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_collision_cooker "${_source}" "${_vice_editor_default_project_file}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
    endforeach()
  endif()

  if(TARGET vice_rigid_material_cooker)
    file(GLOB CONFIGURE_DEPENDS _vice_editor_material_sources "${_vice_editor_default_asset_source_root}/materials/*.vrigmat")
    foreach(_source IN LISTS _vice_editor_material_sources)
      get_filename_component(_name "${_source}" NAME_WE)
      set(_output "${_vice_editor_default_cooked_root}/materials/${_name}.vrigmatc")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_vice_editor_default_cooked_root}/materials"
        COMMAND $<TARGET_FILE:vice_rigid_material_cooker> --input "${_source}" --output "${_output}"
        DEPENDS vice_rigid_material_cooker "${_source}" "${_vice_editor_default_project_file}"
        VERBATIM
      )
      list(APPEND _vice_editor_asset_outputs "${_output}")
    endforeach()
  endif()

  if(_vice_editor_asset_outputs)
    add_custom_target(vice_editor_asset_cache DEPENDS ${_vice_editor_asset_outputs})
    add_dependencies(vice_editor vice_editor_asset_cache)
  endif()
endif()
