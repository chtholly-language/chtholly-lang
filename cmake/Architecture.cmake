# SPDX-License-Identifier: MIT OR Apache-2.0
# Generation emits evaluated target properties; File API supplies the independent
# codemodel (including compile groups and actual build dependency identities).
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/.cmake/api/v1/query/client-chtholly")
file(WRITE "${PROJECT_BINARY_DIR}/.cmake/api/v1/query/client-chtholly/query.json"
  "{\"requests\":[{\"kind\":\"codemodel\",\"version\":2},{\"kind\":\"cache\",\"version\":2}]}")
function(chtholly_export_directory directory)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    if(target MATCHES "^chtholly")
      get_target_property(public_links ${target} INTERFACE_LINK_LIBRARIES)
      if(NOT public_links)
        set(public_links "")
      endif()
      # LINK_ONLY is intentionally private propagation for static link closure.
      string(REGEX REPLACE "\\$<LINK_ONLY:[^<>]*>" "" public_links "${public_links}")
      get_target_property(direct_links ${target} LINK_LIBRARIES)
      get_target_property(target_type ${target} TYPE)
      if(target_type STREQUAL "INTERFACE_LIBRARY")
        get_target_property(direct_links ${target} INTERFACE_LINK_LIBRARIES)
      endif()
      get_target_property(public_includes ${target} INTERFACE_INCLUDE_DIRECTORIES)
      if(NOT direct_links)
        set(direct_links "")
      endif()
      if(NOT public_includes)
        set(public_includes "")
      endif()
      file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/architecture/${target}.txt" CONTENT
"name=${target}
type=$<TARGET_PROPERTY:${target},TYPE>
links=${direct_links}
interface_links=${public_links}
includes=$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>
public_includes=${public_includes}
definitions=$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>
")
    endif()
  endforeach()
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    chtholly_export_directory("${child}")
  endforeach()
endfunction()
chtholly_export_directory("${PROJECT_SOURCE_DIR}")

if(BUILD_TESTING)
  add_custom_target(chtholly_public_headers ALL)
  foreach(module IN ITEMS target source diagnostic lex parse numeric sem_ir check ir lower runtime codegen driver)
    if(CHTHOLLY_FRONTEND_ONLY AND module MATCHES "^(runtime|codegen|driver)$")
      continue()
    endif()
    # This enumerates public headers, not test cases. Each file is compiled alone.
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/toolchain/${module}/include/*.h")
    foreach(header IN LISTS headers)
      file(RELATIVE_PATH include_name "${PROJECT_SOURCE_DIR}/toolchain/${module}/include" "${header}")
      string(MAKE_C_IDENTIFIER "${include_name}" header_id)
      set(source "${PROJECT_BINARY_DIR}/header-check/${header_id}.cpp")
      file(WRITE "${source}" "#include <${include_name}>\n")
      add_library(header_check_${header_id} OBJECT EXCLUDE_FROM_ALL "${source}")
      target_link_libraries(header_check_${header_id} PRIVATE chtholly_${module})
      chtholly_configure_target(header_check_${header_id})
      add_dependencies(chtholly_public_headers header_check_${header_id})
    endforeach()
  endforeach()
endif()
