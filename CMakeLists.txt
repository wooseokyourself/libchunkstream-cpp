cmake_minimum_required(VERSION 3.10)

# Set project name and version
project(chunkstream 
    VERSION 1.0.0
    LANGUAGES CXX
)

# Use C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories for all targets
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")

# Find required packages with platform-specific settings
if(WIN32)
    # On Windows, use CONFIG mode for packages from vcpkg
    find_package(asio CONFIG REQUIRED)
else()
    # On Linux/Ubuntu, use module mode 
    find_package(asio REQUIRED)
endif()

# Output package information
message(STATUS "Asio found: ${Asio_FOUND}")

# Server library source files
set(SENDER_SOURCES
    src/thread_pool.cpp
    src/sender.cpp
    ${COMMON_SOURCES}
)

# Client library source files
set(RECEIVER_SOURCES
    src/receiver.cpp
    ${COMMON_SOURCES}
)

# Header files
set(COMMON_HEADERS
    include/chunkstream/chunk_header.h
)

set(SENDER_HEADERS
    include/chunkstream/sender.h
    ${COMMON_HEADERS}
)

set(RECEIVER_HEADERS
    include/chunkstream/core/ordered_hash_container.h
    include/chunkstream/core/thread_pool.h
    include/chunkstream/receiver.h
    ${COMMON_HEADERS}
)

# Build Mode - Default is static library, can be changed via option
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Set appropriate flags based on library type
if(BUILD_SHARED_LIBS)
    message(STATUS "Building chunkstream as SHARED libraries")
else()
    message(STATUS "Building chunkstream as STATIC libraries")
endif()

# Create library targets with the selected type (STATIC or SHARED based on BUILD_SHARED_LIBS)
add_library(chunkstream_server ${SENDER_SOURCES} ${SENDER_HEADERS})
add_library(chunkstream_client ${RECEIVER_SOURCES} ${RECEIVER_HEADERS})

# Set library properties
set_target_properties(chunkstream_server PROPERTIES
    DEBUG_POSTFIX "d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    POSITION_INDEPENDENT_CODE ON
)

set_target_properties(chunkstream_client PROPERTIES
    DEBUG_POSTFIX "d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    POSITION_INDEPENDENT_CODE ON
)

# Configure export symbols for shared libraries on Windows
if(BUILD_SHARED_LIBS AND WIN32)
    set_target_properties(chunkstream_server PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set_target_properties(chunkstream_client PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Set include directories and link dependencies for server
target_include_directories(chunkstream_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/chunkstream
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Set include directories and link dependencies for client
target_include_directories(chunkstream_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/chunkstream
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Platform-specific include directories and link libraries
if(WIN32)
    target_link_libraries(chunkstream_server PRIVATE asio::asio)
    target_link_libraries(chunkstream_client PRIVATE asio::asio)
else()
    target_include_directories(chunkstream_server PRIVATE ${Asio_INCLUDE_DIRS})
    target_include_directories(chunkstream_client PRIVATE ${Asio_INCLUDE_DIRS})
    target_link_libraries(chunkstream_server PRIVATE ${Asio_LIBRARIES})
    target_link_libraries(chunkstream_client PRIVATE ${Asio_LIBRARIES})
endif()

# Configure static runtime linking for MSVC
if(MSVC AND NOT BUILD_SHARED_LIBS)
    # Static runtime linking for static libraries (/MT or /MTd)
    set_property(TARGET chunkstream_server PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET chunkstream_client PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Create example executables
add_executable(server_example src/server_example.cpp)
add_executable(client_example src/client_example.cpp)

# Set the example executable output directories
set_target_properties(server_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
)

set_target_properties(client_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
)

# Link examples with libraries and ensure correct build order
target_link_libraries(server_example PRIVATE chunkstream_server)
target_link_libraries(client_example PRIVATE chunkstream_client)

# Configure static runtime linking for MSVC example executables
if(MSVC AND NOT BUILD_SHARED_LIBS)
    set_property(TARGET server_example PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET client_example PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Installation settings
include(GNUInstallDirs)
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# Install libraries
install(TARGETS chunkstream_server chunkstream_client
    EXPORT ${PROJECT_NAME}Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install header files
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Install CMake targets
install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${INSTALL_CONFIGDIR}
)

# Generate and install Config files
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/chunkstreamConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

# Generate version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)