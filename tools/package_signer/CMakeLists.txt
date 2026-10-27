add_executable(
  vice_package_signer
  main.cpp
)

target_link_libraries(
  vice_package_signer
  PRIVATE
    VICE::Platform
    VICE::Runtime
)

target_compile_features(vice_package_signer PRIVATE cxx_std_23)
vice_enable_warnings(vice_package_signer)
