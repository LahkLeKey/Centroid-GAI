cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(centroid_code_helper LANGUAGES C)
  find_package(Centroid 0.1.0 EXACT CONFIG REQUIRED COMPONENTS Runtime)
endif()
add_executable(centroid_code_helper main.c)
set_target_properties(centroid_code_helper PROPERTIES C_STANDARD 11
  C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_link_libraries(centroid_code_helper PRIVATE Centroid::Runtime)
if(WIN32)
  # Both build-tree and installed-source hosts need the DLL beside the executable.
  get_target_property(_centroid_runtime_type Centroid::Runtime TYPE)
  if(_centroid_runtime_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET centroid_code_helper POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Centroid::Runtime>" "$<TARGET_FILE_DIR:centroid_code_helper>")
  endif()
endif()
if(MSVC)
  target_compile_options(centroid_code_helper PRIVATE /W4)
else()
  target_compile_options(centroid_code_helper PRIVATE -Wall -Wextra -Wpedantic)
endif()
