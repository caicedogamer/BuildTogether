cmake_minimum_required(VERSION 3.21)
project(EditorP2P VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EP2P_BUILD_TESTS "Build EditorP2P unit tests" OFF)
option(EP2P_BUILD_MOD "Build the Geode mod package" ON)

# Collect all source files
file(GLOB_RECURSE EP2P_SOURCES CONFIGURE_DEPENDS src/*.cpp)

if(EP2P_BUILD_MOD)
    add_library(${PROJECT_NAME} SHARED ${EP2P_SOURCES})

    target_include_directories(${PROJECT_NAME} PRIVATE
        include
    )

    # Windows-specific compile definitions
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601   # Windows 7+ minimum
        EP2P_WINDOWS
    )

    # Geode SDK
    if(NOT DEFINED ENV{GEODE_SDK})
        message(FATAL_ERROR
            "GEODE_SDK environment variable is not set.\n"
            "Install the Geode CLI and run 'geode sdk install'."
        )
    else()
        message(STATUS "Geode SDK: $ENV{GEODE_SDK}")
    endif()

    add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
    setup_geode_mod(${PROJECT_NAME})

    target_link_libraries(${PROJECT_NAME}
        geode-sdk
        ws2_32      # Winsock2 for P2P networking
    )
endif()

if(EP2P_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
