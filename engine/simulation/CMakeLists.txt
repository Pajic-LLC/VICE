add_library(
  vice_simulation
  STATIC
    src/simulation.cpp
)

add_library(VICE::Simulation ALIAS vice_simulation)

target_include_directories(
  vice_simulation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(vice_simulation PUBLIC cxx_std_23)
vice_enable_warnings(vice_simulation)

add_library(
  vice_simulation_world
  STATIC
    src/entity_bindings.cpp
)

add_library(VICE::SimulationWorld ALIAS vice_simulation_world)

target_include_directories(
  vice_simulation_world
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_simulation_world
  PUBLIC
    VICE::Simulation
    VICE::World
)
target_compile_features(vice_simulation_world PUBLIC cxx_std_23)
vice_enable_warnings(vice_simulation_world)
