find_package(Threads REQUIRED)

add_library(
  vice_jobs
  STATIC
    src/jobs.cpp
)

add_library(VICE::Jobs ALIAS vice_jobs)

target_include_directories(
  vice_jobs
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(vice_jobs PUBLIC Threads::Threads)
target_compile_features(vice_jobs PUBLIC cxx_std_23)
vice_enable_warnings(vice_jobs)
