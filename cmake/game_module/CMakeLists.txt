cmake_minimum_required(VERSION 3.28)

project(VICEProjectGameModule LANGUAGES CXX)

foreach(_required IN ITEMS VICE_GAME_SOURCE VICE_GAME_TARGET VICE_GAME_MODULE_INCLUDE VICE_GAME_OUTPUT)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

if(NOT EXISTS "${VICE_GAME_SOURCE}")
  message(FATAL_ERROR "Project game source does not exist: ${VICE_GAME_SOURCE}")
endif()

add_library(vice_project_game MODULE "${VICE_GAME_SOURCE}")

target_include_directories(
  vice_project_game
  PRIVATE
    "${VICE_GAME_MODULE_INCLUDE}"
)

target_compile_features(vice_project_game PRIVATE cxx_std_23)
set_target_properties(
  vice_project_game
  PROPERTIES
    CXX_EXTENSIONS OFF
    PREFIX ""
    OUTPUT_NAME "${VICE_GAME_TARGET}"
    LIBRARY_OUTPUT_DIRECTORY "${VICE_GAME_OUTPUT}"
    RUNTIME_OUTPUT_DIRECTORY "${VICE_GAME_OUTPUT}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

foreach(_config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${_config}" _config_upper)
  set_target_properties(
    vice_project_game
    PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${_config_upper} "${VICE_GAME_OUTPUT}"
      RUNTIME_OUTPUT_DIRECTORY_${_config_upper} "${VICE_GAME_OUTPUT}"
  )
endforeach()

if(APPLE)
  set_target_properties(vice_project_game PROPERTIES SUFFIX ".dylib")
endif()
