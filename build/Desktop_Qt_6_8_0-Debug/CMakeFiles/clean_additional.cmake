# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/cutefin_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/cutefin_autogen.dir/ParseCache.txt"
  "cutefin_autogen"
  )
endif()
