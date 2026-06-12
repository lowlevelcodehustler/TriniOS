cmake_minimum_required(VERSION 3.20)
project(bsd_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# TriniOS BSD Compatibility Server
# ============================================================================

# Mach headers location (from GNU Mach build)
set(MACH_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/kernel/gnumach/build/include")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MACH_INCLUDE_DIR}
)

# ============================================================================
# BSD Server Sources
# ============================================================================

set(BSD_SERVER_SOURCES
    src/main.c
    src/process.c
    src/file.c
    src/pipe.c
    src/signal.c
)

# ============================================================================
# BSD Server Binary
# ============================================================================

add_executable(bsd_server ${BSD_SERVER_SOURCES})

target_compile_options(bsd_server PRIVATE
    -Wall
    -Wextra
    -O2
    -fPIC
)

target_link_options(bsd_server PRIVATE
    -static
)

# ============================================================================
# BSD Client Library (for user-space apps to call BSD server)
# ============================================================================

add_library(bsd_client STATIC
    src/posix_client.c
)

target_include_directories(bsd_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(bsd_client PRIVATE
    -Wall
    -Wextra
    -O2
)

# ============================================================================
# Installation
# ============================================================================

install(TARGETS bsd_server
    DESTINATION ${CMAKE_INSTALL_PREFIX}/sbin
)

install(TARGETS bsd_client
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include/bsd
)

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()