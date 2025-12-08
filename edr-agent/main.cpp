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
#include "version.h"

#include <iostream>
#include <string>
#include <cstring>

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
    std::cout << "  --help, -h    Show this help message\n";
    std::cout << "  --version, -v Show version information\n\n";
    
    std::cout << "Examples:\n";
    std::cout << "  " << exeName << " --install       # Install service\n";
    std::cout << "  net start EDRAgent             # Start service\n";
    std::cout << "  net stop EDRAgent              # Stop service\n";
    std::cout << "  " << exeName << " --uninstall    # Remove service\n\n";
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
    // Initialize logging early
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
        
        return runAgent(false);  // false = not service mode
    }
}
