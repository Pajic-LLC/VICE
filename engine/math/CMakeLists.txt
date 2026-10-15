add_library(vice_math INTERFACE)
add_library(VICE::Math ALIAS vice_math)

target_include_directories(
  vice_math
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(vice_math INTERFACE cxx_std_23)
