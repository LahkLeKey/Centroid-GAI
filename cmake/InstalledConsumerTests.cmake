# CTest orchestrates native commands declaratively. The consumer is strict C11
# and is copied out of the source tree before configuration. Both its package
# prefix and its source live under a dedicated build-tree contract directory.
set(_centroid_package_root "${CMAKE_CURRENT_BINARY_DIR}/installed-consumer")
set(_centroid_package_stage "${_centroid_package_root}/stage")
set(_centroid_package_prefix "${_centroid_package_root}/relocated")
set(_centroid_package_source "${_centroid_package_root}/consumer-source")
set(_centroid_package_build "${_centroid_package_root}/consumer-build")
add_test(NAME package_install
  COMMAND ${CMAKE_COMMAND} --install "${CMAKE_CURRENT_BINARY_DIR}"
    --prefix "${_centroid_package_stage}" --component Runtime --config $<CONFIG>)
add_test(NAME package_relocate
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${_centroid_package_stage}" "${_centroid_package_prefix}")
add_test(NAME package_detach_consumer
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/package" "${_centroid_package_source}")
set(_centroid_consumer_configure
  ${CMAKE_COMMAND} -S "${_centroid_package_source}" -B "${_centroid_package_build}"
  -G "${CMAKE_GENERATOR}"
  "-DCMAKE_PREFIX_PATH=${_centroid_package_prefix}"
  "-DCENTROID_TEST_PREFIX=${_centroid_package_prefix}"
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
  "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
  "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
if(CMAKE_MAKE_PROGRAM)
  list(APPEND _centroid_consumer_configure "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif()
if(CMAKE_GENERATOR_PLATFORM)
  list(APPEND _centroid_consumer_configure -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
  list(APPEND _centroid_consumer_configure -T "${CMAKE_GENERATOR_TOOLSET}")
endif()
add_test(NAME package_configure COMMAND ${_centroid_consumer_configure})
add_test(NAME package_build
  COMMAND ${CMAKE_COMMAND} --build "${_centroid_package_build}" --config $<CONFIG>)
add_test(NAME package_run
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${_centroid_package_build}"
    -C $<CONFIG> --output-on-failure)
# Fixtures ensure `ctest -R package_run` still installs and relocates first.
set_tests_properties(package_install PROPERTIES FIXTURES_SETUP package_installed)
set_tests_properties(package_relocate PROPERTIES
  FIXTURES_REQUIRED package_installed FIXTURES_SETUP package_relocated)
set_tests_properties(package_detach_consumer PROPERTIES FIXTURES_SETUP package_source)
set_tests_properties(package_configure PROPERTIES
  FIXTURES_REQUIRED "package_relocated;package_source" FIXTURES_SETUP package_configured)
set_tests_properties(package_build PROPERTIES
  FIXTURES_REQUIRED package_configured FIXTURES_SETUP package_built)
set_tests_properties(package_run PROPERTIES FIXTURES_REQUIRED package_built)
if(CENTROID_BUILD_EXAMPLES)
  add_test(NAME package_example_install
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_CURRENT_BINARY_DIR}"
      --prefix "${_centroid_package_stage}" --component Examples --config $<CONFIG>)
  add_test(NAME package_example_run
    COMMAND "${_centroid_package_stage}/${CMAKE_INSTALL_BINDIR}/centroid_code_helper${CMAKE_EXECUTABLE_SUFFIX}"
      --context-lifecycle)
  set_tests_properties(package_example_install PROPERTIES
    FIXTURES_REQUIRED package_installed FIXTURES_SETUP package_example_installed)
  set_tests_properties(package_example_run PROPERTIES
    FIXTURES_REQUIRED package_example_installed)
endif()
if(CENTROID_BUILD_TRAINING)
  set(_centroid_training_consumer_build "${_centroid_package_root}/training-consumer-build")
  add_test(NAME package_training_install
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_CURRENT_BINARY_DIR}"
      --prefix "${_centroid_package_stage}" --component Training --config $<CONFIG>)
  add_test(NAME package_training_relocate
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${_centroid_package_stage}" "${_centroid_package_prefix}")
  set(_centroid_training_configure ${_centroid_consumer_configure})
  list(FIND _centroid_training_configure "${_centroid_package_build}" _centroid_build_index)
  list(REMOVE_AT _centroid_training_configure ${_centroid_build_index})
  list(INSERT _centroid_training_configure ${_centroid_build_index}
    "${_centroid_training_consumer_build}")
  list(APPEND _centroid_training_configure -DCENTROID_TEST_TRAINING=ON)
  add_test(NAME package_training_configure COMMAND ${_centroid_training_configure})
  add_test(NAME package_training_build
    COMMAND ${CMAKE_COMMAND} --build "${_centroid_training_consumer_build}" --config $<CONFIG>)
  add_test(NAME package_training_run
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${_centroid_training_consumer_build}"
      -C $<CONFIG> --output-on-failure)
  set_tests_properties(package_run PROPERTIES FIXTURES_SETUP package_runtime_tested)
  set_tests_properties(package_training_install PROPERTIES
    FIXTURES_REQUIRED package_installed FIXTURES_SETUP package_training_installed)
  set_tests_properties(package_training_relocate PROPERTIES
    FIXTURES_REQUIRED "package_runtime_tested;package_training_installed"
    FIXTURES_SETUP package_training_relocated)
  set_tests_properties(package_training_configure PROPERTIES
    FIXTURES_REQUIRED "package_training_relocated;package_source"
    FIXTURES_SETUP package_training_configured)
  set_tests_properties(package_training_build PROPERTIES
    FIXTURES_REQUIRED package_training_configured FIXTURES_SETUP package_training_built)
  set_tests_properties(package_training_run PROPERTIES FIXTURES_REQUIRED package_training_built)
endif()
