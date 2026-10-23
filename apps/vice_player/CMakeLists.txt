add_executable(
  vice_player
  app.cpp
  package_metadata_slot.cpp
)

target_link_libraries(
  vice_player
  PRIVATE
    VICE::Runtime
)

target_compile_features(vice_player PRIVATE cxx_std_23)
vice_enable_warnings(vice_player)
