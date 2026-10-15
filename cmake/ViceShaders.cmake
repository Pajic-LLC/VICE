# Shader cooking helpers for VICE targets.

function(vice_find_dxc out_var)
  if(DEFINED VICE_DXC_EXECUTABLE AND EXISTS "${VICE_DXC_EXECUTABLE}")
    set(${out_var} "${VICE_DXC_EXECUTABLE}" PARENT_SCOPE)
    return()
  endif()

  if(DEFINED ENV{VICE_DXC} AND EXISTS "$ENV{VICE_DXC}")
    set(${out_var} "$ENV{VICE_DXC}" PARENT_SCOPE)
    return()
  endif()

  find_program(_vice_dxc NAMES dxc dxc.exe)
  if(_vice_dxc)
    set(${out_var} "${_vice_dxc}" PARENT_SCOPE)
    return()
  endif()

  if(WIN32)
    cmake_host_system_information(
      RESULT _vice_kits_root
      QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
      VALUE KitsRoot10
      VIEW BOTH
      ERROR_VARIABLE _vice_kits_error
    )
    if(_vice_kits_root AND NOT _vice_kits_error)
      file(TO_CMAKE_PATH "${_vice_kits_root}" _vice_kits_root_cmake)
      file(GLOB _vice_sdk_dxc_candidates
        "${_vice_kits_root_cmake}/bin/*/x64/dxc.exe"
      )
      if(_vice_sdk_dxc_candidates)
        list(SORT _vice_sdk_dxc_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET _vice_sdk_dxc_candidates 0 _vice_sdk_dxc)
        set(${out_var} "${_vice_sdk_dxc}" PARENT_SCOPE)
        return()
      endif()
    endif()
  endif()

  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(vice_find_metal_shader_converter out_var)
  if(DEFINED VICE_METAL_SHADER_CONVERTER_EXECUTABLE AND
     EXISTS "${VICE_METAL_SHADER_CONVERTER_EXECUTABLE}")
    set(${out_var} "${VICE_METAL_SHADER_CONVERTER_EXECUTABLE}" PARENT_SCOPE)
    return()
  endif()

  if(DEFINED ENV{VICE_METAL_SHADER_CONVERTER} AND
     EXISTS "$ENV{VICE_METAL_SHADER_CONVERTER}")
    set(${out_var} "$ENV{VICE_METAL_SHADER_CONVERTER}" PARENT_SCOPE)
    return()
  endif()

  find_program(_vice_metal_shader_converter NAMES metal-shaderconverter metal-shaderconverter.exe)
  if(_vice_metal_shader_converter)
    set(${out_var} "${_vice_metal_shader_converter}" PARENT_SCOPE)
    return()
  endif()

  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(vice_add_cooked_shader)
  set(options)
  set(one_value_args NAME INPUT OUTPUT ENTRY STAGE TARGET DXC METAL_SHADER_CONVERTER)
  cmake_parse_arguments(VICE_SHADER "${options}" "${one_value_args}" "" ${ARGN})

  foreach(required NAME INPUT OUTPUT ENTRY STAGE TARGET)
    if(NOT VICE_SHADER_${required})
      message(FATAL_ERROR "vice_add_cooked_shader requires ${required}.")
    endif()
  endforeach()

  if(NOT TARGET vice_shader_cooker)
    message(FATAL_ERROR "vice_add_cooked_shader requires the vice_shader_cooker target.")
  endif()

  set(_vice_shader_outputs
    "${VICE_SHADER_OUTPUT}/shader.dxil"
    "${VICE_SHADER_OUTPUT}/shader.dxc.txt"
    "${VICE_SHADER_OUTPUT}/shader.vice.json"
  )
  if(VICE_SHADER_TARGET STREQUAL "metal" OR VICE_SHADER_TARGET STREQUAL "all")
    list(APPEND _vice_shader_outputs
      "${VICE_SHADER_OUTPUT}/shader.metallib"
      "${VICE_SHADER_OUTPUT}/shader.metal.json"
    )
  endif()

  set(_vice_shader_command
    $<TARGET_FILE:vice_shader_cooker>
    --input "${VICE_SHADER_INPUT}"
    --output "${VICE_SHADER_OUTPUT}"
    --entry "${VICE_SHADER_ENTRY}"
    --stage "${VICE_SHADER_STAGE}"
    --target "${VICE_SHADER_TARGET}"
  )
  if(VICE_SHADER_DXC)
    list(APPEND _vice_shader_command --dxc "${VICE_SHADER_DXC}")
  endif()
  if(VICE_SHADER_METAL_SHADER_CONVERTER)
    list(APPEND _vice_shader_command
      --metal-shader-converter "${VICE_SHADER_METAL_SHADER_CONVERTER}"
    )
  endif()

  add_custom_command(
    OUTPUT ${_vice_shader_outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VICE_SHADER_OUTPUT}"
    COMMAND ${_vice_shader_command}
    DEPENDS vice_shader_cooker "${VICE_SHADER_INPUT}"
    COMMENT "Cooking VICE shader ${VICE_SHADER_NAME}"
    VERBATIM
  )

  add_custom_target(${VICE_SHADER_NAME} DEPENDS ${_vice_shader_outputs})
endfunction()
