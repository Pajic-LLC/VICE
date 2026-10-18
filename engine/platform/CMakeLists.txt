add_library(vice_platform STATIC)
add_library(VICE::Platform ALIAS vice_platform)

target_include_directories(
  vice_platform
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(vice_platform PUBLIC VICE::Core)
target_compile_features(vice_platform PUBLIC cxx_std_23)
vice_enable_warnings(vice_platform)

if(APPLE)
  enable_language(OBJCXX)

  target_sources(
    vice_platform
    PRIVATE
      src/macos/dynamic_library.cpp
      src/macos/platform.mm
      src/macos/process.mm
  )
  set_target_properties(
    vice_platform
    PROPERTIES
      OBJCXX_STANDARD 23
      OBJCXX_STANDARD_REQUIRED YES
      OBJCXX_EXTENSIONS NO
  )
  set_source_files_properties(
    src/macos/platform.mm
    src/macos/process.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
  )

  find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
  target_link_libraries(vice_platform PRIVATE ${APPKIT_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
elseif(WIN32)
  target_sources(
    vice_platform
    PRIVATE
      src/windows/dynamic_library.cpp
      src/windows/platform.cpp
      src/windows/process.cpp
  )
  target_compile_definitions(vice_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(vice_platform PRIVATE user32 shell32 ole32)
else()
  message(FATAL_ERROR "VICE currently supports macOS and Windows as development targets.")
endif()
