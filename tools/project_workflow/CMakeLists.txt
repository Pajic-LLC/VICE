add_library(
  vice_project_workflow
  STATIC
    src/project_workflow.cpp
    src/project_build.cpp
    src/project_session.cpp
)
add_library(VICE::ProjectWorkflow ALIAS vice_project_workflow)

target_include_directories(
  vice_project_workflow
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_project_workflow
  PUBLIC
    VICE::Project
  PRIVATE
    VICE::Platform
    VICE::Runtime
    VICE::RuntimeDevSession
    VICE::World
    VICE::WorldCooker
    VICE::MeshCooker
    VICE::TextureCooker
    VICE::EnvironmentCooker
    VICE::RenderMaterialCooker
    VICE::PresentationCooker
)
target_compile_features(vice_project_workflow PUBLIC cxx_std_23)
target_compile_definitions(
  vice_project_workflow
  PRIVATE
    VICE_PRESENTATION_NOTICES="${PROJECT_SOURCE_DIR}/docs/licenses/presentation.txt"
    VICE_PROJECT_WORKFLOW_CMAKE_EXECUTABLE="${CMAKE_COMMAND}"
    VICE_PROJECT_WORKFLOW_GAME_MODULE_CMAKE_SOURCE="${PROJECT_SOURCE_DIR}/cmake/game_module"
    VICE_PROJECT_WORKFLOW_GAME_MODULE_INCLUDE="${PROJECT_SOURCE_DIR}/engine/runtime/include"
)
vice_enable_warnings(vice_project_workflow)