cmake_minimum_required(VERSION 3.15)

# CRITICAL: Must be BEFORE project() to match vcpkg x64-windows-static triplet
# vcpkg static libs use /MT, so we MUST use /MT too
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(edr-agent VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC requires /bigobj for Boost.Beast SSL templates (many sections)
if(MSVC)
    add_compile_options(/bigobj)
endif()

# ==========================================
# Version Header Generation
# ==========================================
# Generate version.h from version.h.in template
# This creates a single source of truth for agent version
configure_file(
    ${CMAKE_SOURCE_DIR}/version.h.in
    ${CMAKE_BINARY_DIR}/version.h
    @ONLY
)
# Include build directory for generated version.h
include_directories(${CMAKE_BINARY_DIR})

# ==========================================
# Options
# ==========================================
option(ENABLE_WEBSOCKET "Build with WebSocket support" OFF)

message(STATUS "========================================")
message(STATUS "  EDR Agent Build Configuration")
message(STATUS "  WebSocket Support: ${ENABLE_WEBSOCKET}")
message(STATUS "========================================")

# ==========================================
# vcpkg Integration
# ==========================================
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

# ==========================================
# Dependencies
# ==========================================
find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml REQUIRED)
find_package(zstd CONFIG REQUIRED)

if(ENABLE_WEBSOCKET)
    # Boost.Beast is part of Boost (no separate library needed)
    # Beast provides WebSocket support built on top of Boost.Asio
    message(STATUS "Using Boost.Beast for WebSocket support")

    # Dependencies (from vcpkg)
    find_package(OpenSSL REQUIRED)
    find_package(Boost REQUIRED COMPONENTS system thread random date_time regex chrono atomic)
endif()

# ==========================================
# Source Files
# ==========================================
set(AGENT_SOURCES
    main.cpp
    EdrAgent.cpp
    HttpClient.cpp
    ConfigReader.cpp
    EventConverter.cpp
    SimpleZstd.cpp
    CommandProcessor.cpp
    AgentId.cpp
    Logger.cpp
    ServiceManager.cpp
    Heartbeat.cpp
    Updater.cpp
    EnrollmentClient.cpp  # NEW: Agent registration with server
    ConfigManager.cpp     # NEW: Agent configuration management
    EventSpillover.cpp
)

if(ENABLE_WEBSOCKET)
    list(APPEND AGENT_SOURCES WebSocketClient.cpp)
    add_compile_definitions(ENABLE_WEBSOCKET)
endif()

# ==========================================
# Target Definition
# ==========================================
add_executable(edr-agent ${AGENT_SOURCES})

# ==========================================
# Linking
# ==========================================
target_link_libraries(edr-agent PRIVATE
    nlohmann_json::nlohmann_json
    pugixml::pugixml
    zstd::libzstd_static
    ws2_32
    winhttp
    wevtapi
    iphlpapi
    bcrypt
    psapi
)

if(ENABLE_WEBSOCKET)
    target_link_libraries(edr-agent PRIVATE
        # Boost.Beast is header-only, just needs Boost libraries
        OpenSSL::SSL
        OpenSSL::Crypto
        Boost::system
        Boost::thread
        Boost::random
        Boost::date_time
        Boost::regex
        Boost::chrono
        Boost::atomic
    )
endif()

# ==========================================
# MSVC Static Linking (/MT)
# ==========================================
if(MSVC)
    message(STATUS "Configuring for MSVC Static Linking (/MT)...")
    
    # Force static runtime for Release builds
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    
    # Add /MT flag to compiler options
    add_compile_options($<$<CONFIG:Release>:/MT>)
    add_compile_options($<$<CONFIG:Debug>:/MTd>)
    
    # Add /bigobj for Boost.Beast SSL templates (many sections)
    add_compile_options(/bigobj)
    
    # Define Windows compatibility
    target_compile_definitions(edr-agent PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        BOOST_ASIO_NO_DEPRECATED  # Use modern Boost.Asio API
    )
endif()

# ==========================================
# Post-Build: Copy config files to output directory
# ==========================================
# MSVC puts output in build/Release or build/Debug, not build/bin
# Use post-build command to copy config files next to the executable

add_custom_command(TARGET edr-agent POST_BUILD
    # Copy config.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:edr-agent>/config.json"
    # Copy sysmon-config.xml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sysmon-config.xml"
        "$<TARGET_FILE_DIR:edr-agent>/sysmon-config.xml"
    COMMENT "Copying config files to output directory..."
)

# Copy auth.secret if it exists (optional file)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/auth.secret")
    add_custom_command(TARGET edr-agent POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/auth.secret"
            "$<TARGET_FILE_DIR:edr-agent>/auth.secret"
        COMMENT "Copying auth.secret to output directory..."
    )
endif()

message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Config files will be copied next to executable")
message(STATUS "========================================")
