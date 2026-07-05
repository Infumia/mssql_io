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

# Install plugin library
install(TARGETS ${PLUGIN_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install native library
install(TARGETS mssql_io DESTINATION ${CMAKE_INSTALL_BINDIR})

