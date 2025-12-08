// ============================================================================
// ServiceManager.hpp - Windows Service Control Manager Integration
// ============================================================================
// Provides Windows Service lifecycle management for the EDR Agent.
// Handles service installation, uninstallation, and runtime control.
// ============================================================================
#pragma once

#include <Windows.h>
#include <string>

// ============================================================================
// Service Configuration Constants
// ============================================================================
// These define the service identity in Windows SCM

// Internal service name (no spaces, used by SCM commands like `sc query`)
#define SERVICE_NAME             L"EDRAgent"

// Display name (shown in services.msc, can have spaces)
#define SERVICE_DISPLAY_NAME     L"EDR Agent Security Service"

// Description (shown in service properties)
#define SERVICE_DESCRIPTION      L"Endpoint Detection and Response Agent - Monitors system events and reports security telemetry."

// Default installation directory
#define SERVICE_INSTALL_PATH     L"C:\\Program Files\\EDRAgent"

// ============================================================================
// ServiceManager Class
// ============================================================================
// Static class providing service management operations.
// All methods are static because we only have one service per process.
// ============================================================================
class ServiceManager {
public:
    // ========================================================================
    // Service Lifecycle Commands (called from main.cpp based on CLI args)
    // ========================================================================
    
    // Install the service in Windows SCM
    // Registers the service with auto-start and LocalSystem account
    // Returns: true on success, false on failure (check GetLastError())
    static bool Install();
    
    // Uninstall the service from Windows SCM
    // Stops the service if running, then removes registration
    // Returns: true on success, false on failure
    static bool Uninstall();
    
    // Run in service mode (called when SCM starts the service)
    // This function blocks until the service is stopped
    // Called with --service flag by SCM
    static void Run();
    
    // ========================================================================
    // Service Status Query
    // ========================================================================
    
    // Check if service is currently installed
    static bool IsInstalled();
    
    // Check if service is currently running
    static bool IsRunning();
    
    // ========================================================================
    // Internal Handlers (called by Windows, not by your code)
    // ========================================================================
private:
    // ServiceMain - Entry point called by SCM after StartServiceCtrlDispatcher
    // This is where service initialization and main loop happen
    static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
    
    // ServiceCtrlHandler - Receives control signals from SCM
    // Handles: STOP, PAUSE, CONTINUE, INTERROGATE, SHUTDOWN
    static void WINAPI ServiceCtrlHandler(DWORD ctrlCode);
    
    // ========================================================================
    // Status Reporting
    // ========================================================================
    
    // Report current service state to SCM
    // state: SERVICE_RUNNING, SERVICE_STOPPED, SERVICE_START_PENDING, etc.
    // exitCode: 0 on success, error code on failure
    // waitHint: Estimated time for pending operation (milliseconds)
    static void ReportStatus(DWORD state, DWORD exitCode, DWORD waitHint);
    
    // ========================================================================
    // State Variables
    // ========================================================================
    
    // Handle for status reporting (obtained from RegisterServiceCtrlHandler)
    static SERVICE_STATUS_HANDLE s_StatusHandle;
    
    // Current service state
    static SERVICE_STATUS s_Status;
    
    // Event signaled when service should stop
    // Main loop waits on this event
    static HANDLE s_StopEvent;
    
    // Checkpoint counter for pending operations
    // SCM uses this to detect hung services
    static DWORD s_CheckPoint;
};

// ============================================================================
// Helper Macros
// ============================================================================

// Log with service context
#define LOG_SVC_INFO(msg)  LOG_INFO("[Service] " + std::string(msg))
#define LOG_SVC_ERROR(msg) LOG_ERROR("[Service] " + std::string(msg))
#define LOG_SVC_WARN(msg)  LOG_WARN("[Service] " + std::string(msg))
