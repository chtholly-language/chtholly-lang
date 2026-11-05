# SPDX-License-Identifier: MIT OR Apache-2.0
# Loaded before project(), including the independent clang-cl runtime probe.
if(NOT CMAKE_HOST_WIN32)
  message(FATAL_ERROR "A Windows x64 Developer Shell is required")
endif()
if("$ENV{WindowsSdkDir}" STREQUAL "" OR "$ENV{WindowsSDKVersion}" STREQUAL "")
  message(FATAL_ERROR "Import the MSVC x64 Developer Shell with WindowsSdkDir and WindowsSDKVersion")
endif()
file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64/mt.exe" chtholly_sdk_mt)
cmake_path(NORMAL_PATH chtholly_sdk_mt)
if(NOT EXISTS "${chtholly_sdk_mt}" OR IS_DIRECTORY "${chtholly_sdk_mt}")
  message(FATAL_ERROR "The selected Windows SDK manifest tool is missing: ${chtholly_sdk_mt}")
endif()
# Never let clang-cl compiler detection fall back to llvm-mt from PATH.
set(CMAKE_MT "${chtholly_sdk_mt}" CACHE FILEPATH "Selected Windows SDK manifest tool" FORCE)
message(STATUS "Windows SDK manifest tool: ${CMAKE_MT}")
unset(chtholly_sdk_mt)
