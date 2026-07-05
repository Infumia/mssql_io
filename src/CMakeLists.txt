cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(mssql_io VERSION 0.0.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Check if FreeTDS is available BEFORE setting sources
set(USE_FREETDS OFF)

# Check if FreeTDS paths were provided via CMake arguments (iOS/Android)
if(DEFINED FREETDS_INCLUDE_DIR AND DEFINED FREETDS_LIBRARY)
    if(EXISTS "${FREETDS_INCLUDE_DIR}/sybdb.h" AND EXISTS "${FREETDS_LIBRARY}")
        set(USE_FREETDS ON)
        message(STATUS "Using FreeTDS from provided paths: ${FREETDS_LIBRARY}")
    else()
        message(WARNING "FreeTDS paths provided but files not found. Building stub implementation.")
    endif()
endif()

# For Android: Check FreeTDS availability first
if(ANDROID AND NOT USE_FREETDS)
    # Check if paths were provided by Gradle
    if(DEFINED FREETDS_INCLUDE_DIR AND DEFINED FREETDS_LIBRARY_DIR)
        # Verify the include directory actually exists and has sybdb.h
        if(EXISTS "${FREETDS_INCLUDE_DIR}/sybdb.h")
            set(USE_FREETDS ON)
            message(STATUS "Using Android FreeTDS from: ${FREETDS_LIBRARY_DIR}")
        else()
            message(WARNING "FreeTDS_INCLUDE_DIR provided but sybdb.h not found. Building stub implementation.")
        endif()
    else()
        # Try to find FreeTDS in Android project structure
        if(ANDROID_ABI)
            find_path(FREETDS_INCLUDE_DIR sybdb.h
                PATHS
                ${CMAKE_CURRENT_SOURCE_DIR}/../android/src/main/jniLibs/${ANDROID_ABI}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/../../android/src/main/jniLibs/${ANDROID_ABI}/include
                NO_DEFAULT_PATH
            )
            
            if(FREETDS_INCLUDE_DIR)
                # Check if library exists
                find_library(FREETDS_LIBRARY
                    NAMES sybdb
                    PATHS
                    ${CMAKE_CURRENT_SOURCE_DIR}/../android/src/main/jniLibs/${ANDROID_ABI}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../android/src/main/jniLibs/${ANDROID_ABI}
                    NO_DEFAULT_PATH
                )
                
                if(FREETDS_LIBRARY)
                    set(USE_FREETDS ON)
                    message(STATUS "Found Android FreeTDS: ${FREETDS_LIBRARY}")
                endif()
            endif()
        endif()
        
        if(NOT USE_FREETDS)
            message(WARNING "FreeTDS not found for Android ${ANDROID_ABI}. Building stub implementation. Run android/build_freetds.sh to build FreeTDS.")
        endif()
    endif()
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT USE_FREETDS)
    # iOS: Check for FreeTDS in Frameworks directory
    # Try to find FreeTDS in common iOS locations
    find_path(FREETDS_INCLUDE_DIR sybdb.h
        PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/../ios/Frameworks/Headers
        ${CMAKE_CURRENT_SOURCE_DIR}/../../ios/Frameworks/Headers
        NO_DEFAULT_PATH
    )
    
    if(FREETDS_INCLUDE_DIR)
        find_library(FREETDS_LIBRARY
            NAMES sybdb libsybdb
            PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/../ios/Frameworks
            ${CMAKE_CURRENT_SOURCE_DIR}/../../ios/Frameworks
            NO_DEFAULT_PATH
        )
        
        if(FREETDS_LIBRARY)
            set(USE_FREETDS ON)
            message(STATUS "Found iOS FreeTDS: ${FREETDS_LIBRARY}")
        endif()
    endif()
    
    if(NOT USE_FREETDS)
        message(WARNING "FreeTDS not found for iOS. Building stub implementation. Run pod install to build FreeTDS.")
    endif()
elseif(NOT USE_FREETDS)
    # Desktop platforms: Find system FreeTDS
    find_path(FREETDS_CHECK sybdb.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include /opt/local/include
    )
    if(FREETDS_CHECK)
        set(USE_FREETDS ON)
        message(STATUS "Found FreeTDS headers at: ${FREETDS_CHECK}")
    else()
        message(WARNING "FreeTDS not found. Building stub implementation. Install FreeTDS for production use.")
    endif()
endif()

# Source files - set AFTER FreeTDS check
# Use CMAKE_CURRENT_SOURCE_DIR to ensure correct path resolution
set(HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/mssql_native.h)

if(USE_FREETDS)
    set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mssql_native.cpp)
    message(STATUS "Building with FreeTDS support")
else()
    set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mssql_native_stub.cpp)
    message(STATUS "Building stub implementation (FreeTDS not available)")
endif()

# Create library (shared for desktop, static for iOS/Android)
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_library(mssql_io STATIC ${SOURCES} ${HEADERS})
elseif(ANDROID)
    add_library(mssql_io SHARED ${SOURCES} ${HEADERS})
else()
    add_library(mssql_io SHARED ${SOURCES} ${HEADERS})
endif()

# Link FreeTDS if available (only if USE_FREETDS is ON)
if(USE_FREETDS)
    if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # iOS: Use provided paths or found paths
        if(DEFINED FREETDS_INCLUDE_DIR AND DEFINED FREETDS_LIBRARY)
            target_include_directories(mssql_io PRIVATE ${FREETDS_INCLUDE_DIR})
            target_link_libraries(mssql_io PRIVATE ${FREETDS_LIBRARY})
        else()
            # Should not happen if USE_FREETDS is ON, but handle gracefully
            message(WARNING "FreeTDS paths not properly configured for iOS, falling back to stub.")
            set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mssql_native_stub.cpp)
            message(STATUS "Reverting to stub implementation due to FreeTDS linking issues.")
        endif()
    elseif(ANDROID)
        # Android: Use provided paths from Gradle or previously found paths
        if(DEFINED FREETDS_INCLUDE_DIR AND DEFINED FREETDS_LIBRARY_DIR)
            target_include_directories(mssql_io PRIVATE ${FREETDS_INCLUDE_DIR})
            target_link_directories(mssql_io PRIVATE ${FREETDS_LIBRARY_DIR})
            target_link_libraries(mssql_io PRIVATE sybdb)
        elseif(DEFINED FREETDS_INCLUDE_DIR AND DEFINED FREETDS_LIBRARY)
            target_include_directories(mssql_io PRIVATE ${FREETDS_INCLUDE_DIR})
            target_link_libraries(mssql_io PRIVATE ${FREETDS_LIBRARY})
        else()
            # This shouldn't happen if USE_FREETDS is ON, but handle gracefully
            message(WARNING "FreeTDS paths not properly configured for Android, but USE_FREETDS is ON")
        endif()
    else()
        # Desktop platforms: Find system FreeTDS
        find_path(FREETDS_INCLUDE_DIR sybdb.h
            PATHS
            /usr/include
            /usr/local/include
            /opt/homebrew/include
            /opt/local/include
            $ENV{FREETDS_HOME}/include
        )

        find_library(FREETDS_LIBRARY
            NAMES sybdb libsybdb
            PATHS
            /usr/lib
            /usr/local/lib
            /opt/homebrew/lib
            /opt/local/lib
            $ENV{FREETDS_HOME}/lib
        )

        if(FREETDS_INCLUDE_DIR AND FREETDS_LIBRARY)
            target_include_directories(mssql_io PRIVATE ${FREETDS_INCLUDE_DIR})
            target_link_libraries(mssql_io PRIVATE ${FREETDS_LIBRARY})
        else()
            message(FATAL_ERROR "FreeTDS not found but USE_FREETDS is ON")
        endif()
    endif()
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(mssql_io PRIVATE WIN32_LEAN_AND_MEAN)
    target_link_libraries(mssql_io PRIVATE ws2_32)
elseif(ANDROID)
    # Android uses its own threading library, no need for pthread
    target_link_libraries(mssql_io PRIVATE dl log)
elseif(APPLE)
    set_target_properties(mssql_io PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER com.mssql.io
        VERSION ${PROJECT_VERSION}
        SOVERSION 3
        PUBLIC_HEADER "${HEADERS}"
    )
elseif(UNIX)
    target_link_libraries(mssql_io PRIVATE pthread dl)
endif()

# Export symbols
set_target_properties(mssql_io PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Install targets
install(TARGETS mssql_io
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    FRAMEWORK DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

