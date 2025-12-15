// ============================================================================
// main.cpp - EDR Agent Entry Point with CLI Argument Parsing
// ============================================================================
// This is the main entry point for the EDR Agent.
// It handles command-line arguments to determine the execution mode:
//
// Usage:
//   edr-agent.exe --install     Install as Windows service
//   edr-agent.exe --uninstall   Uninstall Windows service
//   edr-agent.exe --service     Run in service mode (called by SCM)
//   edr-agent.exe --console     Run in console mode (for debugging)
//   edr-agent.exe --status      Check service status
//   edr-agent.exe --help        Show help message
//   edr-agent.exe               Default: console mode
//
// ============================================================================

#include "ServiceManager.hpp"
#include "Logger.hpp"
#include "ConfigReader.hpp"
#include "EnrollmentClient.hpp"  // Agent registration
#include "AgentId.hpp"           // For g_agentId initialization
#include "version.h"

#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <filesystem>  // For std::filesystem::current_path()

// External references to globals from EdrAgent.cpp
extern std::string g_agentId;

// Forward declaration of agent main function
extern int runAgent(bool serviceMode);

// ============================================================================
// Print Help Message
// ============================================================================
void printHelp(const char* exeName) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  EDR Agent v" << AGENT_VERSION << "\n";
    std::cout << "  Endpoint Detection and Response\n";
    std::cout << "========================================\n\n";
    
    std::cout << "Usage: " << exeName << " [option]\n\n";
    
    std::cout << "Service Management:\n";
    std::cout << "  --install     Install as Windows service (requires Admin)\n";
    std::cout << "  --uninstall   Uninstall Windows service (requires Admin)\n";
    std::cout << "  --status      Check if service is installed/running\n\n";
    
    std::cout << "Execution Modes:\n";
    std::cout << "  --service     Run in service mode (called by SCM)\n";
    std::cout << "  --console     Run in console mode (interactive)\n";
    std::cout << "  (no args)     Default: console mode\n\n";
    
    std::cout << "Other:\n";
    std::cout << "  --enable-polling   Enable HTTP command polling (fallback if WebSocket unavailable)\n";
    std::cout << "  --disable-polling  Disable HTTP command polling\n";
    std::cout << "  --help, -h         Show this help message\n";
    std::cout << "  --version, -v      Show version information\n\n";
    
    std::cout << "Examples:\n";
    std::cout << "  " << exeName << " --install       # Install service\n";
    std::cout << "  net start EDRAgent             # Start service\n";
    std::cout << "  net stop EDRAgent              # Stop service\n";
    std::cout << "  " << exeName << " --uninstall    # Remove service\n";
    std::cout << "  " << exeName << " --enable-polling  # Enable HTTP polling in config\n\n";
}

// ============================================================================
// Print Version Information
// ============================================================================
void printVersion() {
    std::cout << "EDR Agent v" << AGENT_VERSION << "\n";
    std::cout << "Build: " << AGENT_BUILD_TYPE << "\n";
}

// ============================================================================
// Print Service Status
// ============================================================================
void printStatus() {
    std::cout << "EDR Agent Service Status\n";
    std::cout << "------------------------\n";
    
    bool installed = ServiceManager::IsInstalled();
    std::cout << "Installed: " << (installed ? "Yes" : "No") << "\n";
    
    if (installed) {
        bool running = ServiceManager::IsRunning();
        std::cout << "Running:   " << (running ? "Yes" : "No") << "\n";
    }
}

// ============================================================================
// Main Entry Point
// ============================================================================
int main(int argc, char* argv[]) {
    // CRITICAL: Set working directory to executable location FIRST
    // Services start from C:\Windows\System32, but we need to be in our app folder
    // This must happen BEFORE logging and config initialization
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) != 0) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash != NULL) {
            *lastSlash = L'\0';
            SetCurrentDirectoryW(exePath);
        }
    }
    
    // Now initialize logging (log file will be in the correct directory)
    Logger::instance().setLevel(LogLevel::LVL_DEBUG);
    Logger::instance().setLogFile("edr-agent.log");
    
    // Default mode: console
    std::string mode = "console";
    
    // Parse command line arguments
    if (argc > 1) {
        std::string arg = argv[1];
        
        // Help
        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            return 0;
        }
        
        // Version
        if (arg == "--version" || arg == "-v") {
            printVersion();
            return 0;
        }
        
        // Status
        if (arg == "--status") {
            printStatus();
            return 0;
        }
        
        // Install service
        if (arg == "--install") {
            LOG_INFO("Attempting to install service...");
            if (ServiceManager::Install()) {
                std::cout << "Service installed successfully!\n";
                std::cout << "Start with: net start EDRAgent\n";
                return 0;
            } else {
                std::cerr << "Failed to install service. Check logs for details.\n";
                std::cerr << "Make sure to run as Administrator.\n";
                return 1;
            }
        }
        
        // Uninstall service
        if (arg == "--uninstall") {
            LOG_INFO("Attempting to uninstall service...");
            if (ServiceManager::Uninstall()) {
                std::cout << "Service uninstalled successfully!\n";
                return 0;
            } else {
                std::cerr << "Failed to uninstall service. Check logs for details.\n";
                std::cerr << "Make sure to run as Administrator.\n";
                return 1;
            }
        }
        
        // Service mode (called by SCM)
        if (arg == "--service") {
            mode = "service";
        }
        
        // Enable HTTP polling
        else if (arg == "--enable-polling") {
            ConfigReader config("config.json");
            nlohmann::json json = config.getJson();
            json["enable_http_polling"] = true;
            json.erase("disable_http_polling");  // Remove old field if present
            std::ofstream file("config.json");
            file << json.dump(2);
            std::cout << "HTTP command polling ENABLED in config.json\n";
            return 0;
        }
        
        // Disable HTTP polling
        else if (arg == "--disable-polling") {
            ConfigReader config("config.json");
            nlohmann::json json = config.getJson();
            json["enable_http_polling"] = false;
            json.erase("disable_http_polling");  // Remove old field if present
            std::ofstream file("config.json");
            file << json.dump(2);
            std::cout << "HTTP command polling DISABLED in config.json\n";
            return 0;
        }
        
        // Console mode (explicit)
        else if (arg == "--console") {
            mode = "console";
        }
        
        // Unknown argument
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            std::cerr << "Use --help for usage information.\n";
            return 1;
        }
    }
    
    // Execute based on mode
    if (mode == "service") {
        // Service mode - let ServiceManager handle SCM communication
        LOG_INFO("Starting in service mode...");
        ServiceManager::Run();
        return 0;
    } else {
        // Console mode - run agent directly with interactive controls
        LOG_INFO("Starting in console mode...");
        std::cout << "========================================\n";
        std::cout << "  EDR Agent v" << AGENT_VERSION << "\n";
        std::cout << "  Console Mode (Press Ctrl+C to stop)\n";
        std::cout << "========================================\n\n";
        
        // ================================================================
        // TEACHING: Enrollment Flow
        // ================================================================
        // Before the agent can send telemetry, it must be "enrolled".
        // 
        // First-time run:
        //   1. Agent reads enrollment_token from config.json
        //   2. Agent calls POST /api/v1/enroll/ with token
        //   3. Server validates and returns identity_token
        //   4. Agent saves identity_token to auth.secret
        //   5. Agent continues with normal operation
        //
        // Subsequent runs:
        //   - auth.secret already has identity_token
        //   - needsEnrollment() returns false
        //   - Skip straight to normal operation
        // ================================================================
        
        // IMPORTANT: Initialize Agent ID BEFORE enrollment
        // The enrollment request requires agent_id in the JSON payload
        std::filesystem::path configDir = std::filesystem::current_path();
        g_agentId = AgentId::getOrCreate(configDir);
        LOG_INFO("Agent ID initialized: " + g_agentId);
        
        ConfigReader enrollConfig("config.json");
        if (enrollConfig.needsEnrollment()) {
            LOG_INFO("Agent not enrolled - starting enrollment process...");
            if (!EnrollmentClient::enrollAgent(enrollConfig)) {
                LOG_ERROR("Agent enrollment failed. Cannot continue.");
                std::cerr << "ENROLLMENT FAILED!\n";
                std::cerr << "Please check:\n";
                std::cerr << "  1. config.json has 'enrollment_token' field\n";
                std::cerr << "  2. Server is reachable\n";
                std::cerr << "  3. Token is valid (not expired/used)\n";
                return 1;
            }
            LOG_INFO("Enrollment successful - continuing startup");
        }
        
        return runAgent(false);  // false = not service mode
    }
}
