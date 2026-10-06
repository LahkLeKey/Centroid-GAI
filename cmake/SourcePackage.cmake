# A source release copies an explicit class of authored inputs. It never asks
# CPack to sweep the working directory, ignored artifacts or deployment assets.
option(CENTROID_PREPARE_SOURCE_PACKAGE "Prepare a manifest-bound native source archive target" OFF)
if(NOT CENTROID_PREPARE_SOURCE_PACKAGE)
  return()
endif()

set(_centroid_source_files)
foreach(_centroid_native_root src include cli tests data/audit)
  file(GLOB_RECURSE _centroid_native_inputs CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${_centroid_native_root}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/${_centroid_native_root}/*.h")
  list(APPEND _centroid_source_files ${_centroid_native_inputs})
endforeach()
file(GLOB_RECURSE _centroid_example_inputs CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/*CMakeLists.txt")
file(GLOB_RECURSE _centroid_cmake_inputs CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake.in")
file(GLOB_RECURSE _centroid_document_inputs CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/docs/*.md"
  "${CMAKE_CURRENT_SOURCE_DIR}/research/*.md")
# Raw attempt sidecars are evidence assets, not source-release documents.
list(FILTER _centroid_document_inputs EXCLUDE REGEX "^research/sdk-release/local-attempts/")
file(GLOB _centroid_model_metadata CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/models/*.md"
  "${CMAKE_CURRENT_SOURCE_DIR}/models/*.json")
file(GLOB_RECURSE _centroid_research_manifests CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/research/*local-artifacts.txt")
list(FILTER _centroid_research_manifests EXCLUDE REGEX "^research/sdk-release/local-attempts/")
list(APPEND _centroid_source_files ${_centroid_example_inputs}
  ${_centroid_cmake_inputs} ${_centroid_document_inputs}
  ${_centroid_model_metadata} ${_centroid_research_manifests})
foreach(_centroid_root_input CMakeLists.txt README.md AGENTS.md PRODUCT_PLAN.md
    .gitignore .gitattributes LICENSE research/local-artifacts.txt
    tests/package/CMakeLists.txt)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_centroid_root_input}")
    list(APPEND _centroid_source_files "${_centroid_root_input}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _centroid_source_files)
list(SORT _centroid_source_files)

set(_centroid_source_records "")
set(_centroid_source_hashes)
set(_centroid_source_total_bytes 0)
get_filename_component(_centroid_source_physical_root
  "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)
foreach(_centroid_source_input IN LISTS _centroid_source_files)
  set(_centroid_source_absolute "${CMAKE_CURRENT_SOURCE_DIR}/${_centroid_source_input}")
  get_filename_component(_centroid_source_physical_input
    "${_centroid_source_absolute}" REALPATH)
  file(RELATIVE_PATH _centroid_source_physical_relative
    "${_centroid_source_physical_root}" "${_centroid_source_physical_input}")
  if(IS_SYMLINK "${_centroid_source_absolute}" OR
     "${_centroid_source_input}" MATCHES "[\t\r\n]" OR
     IS_ABSOLUTE "${_centroid_source_physical_relative}" OR
     "${_centroid_source_physical_relative}" MATCHES "^\\.\\.(/|$)")
    message(FATAL_ERROR "Source package requires ordinary input paths: ${_centroid_source_input}")
  endif()
  file(SIZE "${_centroid_source_absolute}" _centroid_source_size)
  file(SHA256 "${_centroid_source_absolute}" _centroid_source_sha256)
  list(APPEND _centroid_source_hashes "${_centroid_source_sha256}")
  math(EXPR _centroid_source_total_bytes "${_centroid_source_total_bytes} + ${_centroid_source_size}")
  string(APPEND _centroid_source_records
    "${_centroid_source_sha256}\t${_centroid_source_size}\t${_centroid_source_input}\n")
endforeach()
string(SHA256 _centroid_source_identity "${_centroid_source_records}")
list(LENGTH _centroid_source_files _centroid_source_file_count)
set(_centroid_source_parent "${CMAKE_CURRENT_BINARY_DIR}/source-package/${_centroid_source_identity}")
set(_centroid_source_directory_name "centroid-${PROJECT_VERSION}-source")
set(_centroid_source_stage "${_centroid_source_parent}/${_centroid_source_directory_name}")
set(_centroid_source_index 0)
foreach(_centroid_source_input IN LISTS _centroid_source_files)
  list(GET _centroid_source_hashes ${_centroid_source_index} _centroid_source_expected_hash)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_centroid_source_input}"
    "${_centroid_source_stage}/${_centroid_source_input}" COPYONLY)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${_centroid_source_input}" _centroid_source_current_hash)
  file(SHA256 "${_centroid_source_stage}/${_centroid_source_input}" _centroid_source_copy_hash)
  if(NOT _centroid_source_current_hash STREQUAL _centroid_source_expected_hash OR
     NOT _centroid_source_copy_hash STREQUAL _centroid_source_expected_hash)
    message(FATAL_ERROR "Source input changed while copying: ${_centroid_source_input}; configure again")
  endif()
  math(EXPR _centroid_source_index "${_centroid_source_index} + 1")
endforeach()
# Manifest records cover exact input bytes; this generated manifest is excluded
# from its own records. Its ordered record body identifies the snapshot path.
file(WRITE "${_centroid_source_stage}/SOURCE_MANIFEST.txt"
  "Centroid native source archive manifest v1\n"
  "record_body_sha256 ${_centroid_source_identity}\n"
  "input_files ${_centroid_source_file_count}\n"
  "input_bytes ${_centroid_source_total_bytes}\n"
  "columns: sha256<TAB>bytes<TAB>relative_path\n\n"
  "${_centroid_source_records}")
set(CENTROID_SOURCE_MANIFEST "${_centroid_source_stage}/SOURCE_MANIFEST.txt"
  CACHE INTERNAL "Exact source archive input manifest" FORCE)
set(CENTROID_SOURCE_STAGE "${_centroid_source_stage}"
  CACHE INTERNAL "Manifest-bound native source snapshot" FORCE)
if(WIN32)
  set(_centroid_source_archive "${CMAKE_CURRENT_BINARY_DIR}/centroid-${PROJECT_VERSION}-source.zip")
  set(_centroid_source_archive_arguments cf "${_centroid_source_archive}" --format=zip)
else()
  set(_centroid_source_archive "${CMAKE_CURRENT_BINARY_DIR}/centroid-${PROJECT_VERSION}-source.tar.gz")
  set(_centroid_source_archive_arguments czf "${_centroid_source_archive}" --format=gnutar)
endif()
add_custom_target(centroid_source_package
  COMMAND ${CMAKE_COMMAND} -E tar ${_centroid_source_archive_arguments}
    "${_centroid_source_directory_name}"
  WORKING_DIRECTORY "${_centroid_source_parent}"
  COMMENT "Archiving ${_centroid_source_file_count} explicit native inputs (${_centroid_source_total_bytes} bytes)"
  VERBATIM)
set(CENTROID_SOURCE_ARCHIVE "${_centroid_source_archive}" CACHE INTERNAL
  "Explicit native source archive output" FORCE)
