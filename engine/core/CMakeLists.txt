add_library(
  vice_core
  STATIC
    src/platform.cpp
    src/version.cpp
)

add_library(VICE::Core ALIAS vice_core)

target_include_directories(
  vice_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(vice_core PUBLIC cxx_std_23)
vice_enable_warnings(vice_core)
