add_executable(vice_lab main.cpp)

target_link_libraries(vice_lab PRIVATE VICE::Core)
target_compile_features(vice_lab PRIVATE cxx_std_23)
vice_enable_warnings(vice_lab)
