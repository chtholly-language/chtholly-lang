# SPDX-License-Identifier: MIT OR Apache-2.0
# ci-analysis loads this before project() detects or probes either compiler.
# Re-evaluation in try_compile uses the same imported Developer Shell SDK.
if(NOT CMAKE_HOST_WIN32)
  message(FATAL_ERROR "The analysis toolchain requires a Windows x64 Developer Shell")
endif()
if("$ENV{WindowsSdkDir}" STREQUAL "" OR "$ENV{WindowsSDKVersion}" STREQUAL "")
  message(FATAL_ERROR "Import the MSVC x64 Developer Shell with WindowsSdkDir and WindowsSDKVersion before configuring analysis")
endif()
file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64/mt.exe" chtholly_sdk_mt)
cmake_path(NORMAL_PATH chtholly_sdk_mt)
if(NOT EXISTS "${chtholly_sdk_mt}" OR IS_DIRECTORY "${chtholly_sdk_mt}")
  message(FATAL_ERROR "The selected Windows SDK manifest tool is missing: ${chtholly_sdk_mt}")
endif()
# Do not fall back to an llvm-mt from PATH: the pinned SDK may lack libxml2.
set(CMAKE_MT "${chtholly_sdk_mt}" CACHE FILEPATH "Windows SDK manifest tool for analysis" FORCE)
message(STATUS "Analysis SDK manifest tool: ${CMAKE_MT}")
unset(chtholly_sdk_mt)
