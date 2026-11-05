# SPDX-License-Identifier: MIT OR Apache-2.0
if(NOT IS_ABSOLUTE "${CHTHOLLY_FILE_API_DIRECTORY}")
  message(FATAL_ERROR "File API preparation requires an absolute build directory")
endif()
file(MAKE_DIRECTORY "${CHTHOLLY_FILE_API_DIRECTORY}/.cmake/api/v1/query")
foreach(query IN ITEMS codemodel-v2 cache-v2)
  file(WRITE "${CHTHOLLY_FILE_API_DIRECTORY}/.cmake/api/v1/query/${query}" "")
endforeach()
