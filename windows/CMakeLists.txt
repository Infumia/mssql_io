cmake_minimum_required(VERSION 3.14)
project(mssql_io_windows VERSION 3.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include parent source directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Add the native library from src
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_BINARY_DIR}/src)

# Plugin library (minimal - FFI handles the real work)
set(PLUGIN_NAME "mssql_io_plugin")

add_library(${PLUGIN_NAME} SHARED
  "mssql_io_plugin.cpp"
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  mssql_io
)

# Debug: Print all relevant install info
message(STATUS "[mssql_io DEBUG] CMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}")
message(STATUS "[mssql_io DEBUG] CMAKE_INSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}")
message(STATUS "[mssql_io DEBUG] CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}")
message(STATUS "[mssql_io DEBUG] CMAKE_CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "[mssql_io DEBUG] CMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}")

# Debug: Print target properties
get_target_property(PLUGIN_LOCATION ${PLUGIN_NAME} IMPORTED_LOCATION)
get_target_property(PLUGIN_SONAME ${PLUGIN_NAME} SONAME)
message(STATUS "[mssql_io DEBUG] PLUGIN_NAME=${PLUGIN_NAME}")
message(STATUS "[mssql_io DEBUG] PLUGIN_LOCATION=${PLUGIN_LOCATION}")
message(STATUS "[mssql_io DEBUG] PLUGIN_SONAME=${PLUGIN_SONAME}")

get_target_property(MSSQL_LOCATION mssql_io IMPORTED_LOCATION)
get_target_property(MSSQL_SONAME mssql_io SONAME)
get_target_property(MSSQL_SOVERSION mssql_io SOVERSION)
message(STATUS "[mssql_io DEBUG] mssql_io LOCATION=${MSSQL_LOCATION}")
message(STATUS "[mssql_io DEBUG] mssql_io SONAME=${MSSQL_SONAME}")
message(STATUS "[mssql_io DEBUG] mssql_io SOVERSION=${MSSQL_SOVERSION}")

# Debug: Check which files exist before install
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/src/Release/mssql_io.dll")
  message(STATUS "[mssql_io DEBUG] Found: src/Release/mssql_io.dll")
elseif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/src/Debug/mssql_io.dll")
  message(STATUS "[mssql_io DEBUG] Found: src/Debug/mssql_io.dll")
else()
  message(WARNING "[mssql_io DEBUG] mssql_io.dll NOT FOUND in src/Release or src/Debug")
  file(GLOB_RECURSE MSSQL_DLLS "${CMAKE_CURRENT_BINARY_DIR}/src/**/mssql_io.dll")
  message(STATUS "[mssql_io DEBUG] GLOB mssql_io DLLs: ${MSSQL_DLLS}")
endif()

# Install with verbose debug output
install(CODE "message(STATUS \"[mssql_io INSTALL] Starting install for mssql_io plugin...\")")
install(CODE "message(STATUS \"[mssql_io INSTALL] CMAKE_INSTALL_PREFIX=\${CMAKE_INSTALL_PREFIX}\")")
install(CODE "message(STATUS \"[mssql_io INSTALL] DESTINATION=\${CMAKE_INSTALL_PREFIX}/\${CMAKE_INSTALL_BINDIR}\")")

install(TARGETS ${PLUGIN_NAME}
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(CODE "message(STATUS \"[mssql_io INSTALL] Installed ${PLUGIN_NAME}\")")

install(TARGETS mssql_io
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(CODE "message(STATUS \"[mssql_io INSTALL] Installed mssql_io\")")
install(CODE "message(STATUS \"[mssql_io INSTALL] Done\")")
