cmake_minimum_required(VERSION 3.15)
project(edr-agent-http VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "========================================")
message(STATUS "  EDR Agent - HTTP Only Mode")
message(STATUS "========================================")

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

# Only required packages (no WebSocket dependencies)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

message(STATUS "Dependencies:")
message(STATUS "  ✓ nlohmann_json: ${nlohmann_json_VERSION}")
message(STATUS "  ✓ pugixml: ${pugixml_VERSION}")

# Source files (HTTP only - no WebSocket files)
set(AGENT_SOURCES
    EdrAgent.cpp      # HTTP-only main file
    HttpClient.cpp
    EventConverter.cpp
    ConfigReader.cpp
)

# Create executable
add_executable(edr-agent-http ${AGENT_SOURCES})

# Link libraries (no websocketpp, no boost)
target_link_libraries(edr-agent-http PRIVATE
    nlohmann_json::nlohmann_json
    pugixml::pugixml
    winhttp      # Windows HTTP (built-in)
    wevtapi      # Windows Event Log (built-in)
    rpcrt4       # UUID generation (built-in)
)

# Windows settings
if(WIN32)
    target_compile_definitions(edr-agent-http PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Copy config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json
    ${CMAKE_BINARY_DIR}/bin/config.json
    COPYONLY
)

message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Output: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "========================================")
